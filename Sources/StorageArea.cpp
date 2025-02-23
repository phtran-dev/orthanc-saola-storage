/**
 * Indexer plugin for Orthanc
 * Copyright (C) 2021 Sebastien Jodogne, UCLouvain, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/

#include "StorageArea.h"
#include "SaolaConfiguration.h"

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <SerializationToolbox.h>
#include <DicomFormat/DicomInstanceHasher.h>
#include <DicomFormat/DicomStreamReader.h>
#include <DicomFormat/DicomMap.h>
#include <Toolbox.h>
#include <SystemToolbox.h>
#include <OrthancException.h>
#include <Logging.h>

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/regex.hpp>

static const boost::regex REGEX_STUDY_DATE("\\d{4}(0[1-9]|1[012])(0[1-9]|[12][0-9]|3[01])");

static const char *EXTENSION = ".symlink";

static const char *const STUDY_INSTANCE_UID = "0020,000d";
static const char *const SERIES_INSTANCE_UID = "0020,000e";
static const char *const PIXEL_DATA = "7fe0,0010";
static const char *const STUDY_DATE = "0008,0020";

static boost::filesystem::path GetPathInternal(const std::string &root,
                                               const std::string &uuid)
{
  if (!Orthanc::Toolbox::IsUuid(uuid))
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }
  else
  {
    assert(!root.empty());

    boost::filesystem::path path = root;
    path /= std::string(&uuid[0], &uuid[2]);
    path /= std::string(&uuid[2], &uuid[4]);
    path /= uuid;

    path.make_preferred();
    return path;
  }
}

static boost::filesystem::path CreateMountDirectory(const std::string &uuid,
                                                    const void *content,
                                                    int64_t size)
{
  if (!Orthanc::Toolbox::IsUuid(uuid))
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }

  assert(!SaolaConfiguration::Instance().GetMountDirectory().empty());

  if (size > 0 && Orthanc::DicomMap::IsDicomFile(content, size))
  {
    // OrthancPlugins::OrthancString s;
    // s.Assign(OrthancPluginDicomBufferToJson(OrthancPlugins::GetGlobalContext(), content, size,
    //                                         OrthancPluginDicomToJsonFormat_Short,
    //                                         OrthancPluginDicomToJsonFlags_None, 256));

    // Json::Value json;
    // s.ToJson(json);

    // static const char* const PATIENT_ID = "0010,0020";
    // static const char* const STUDY_INSTANCE_UID = "0020,000d";
    // static const char* const SERIES_INSTANCE_UID = "0020,000e";
    // static const char* const SOP_INSTANCE_UID = "0008,0018";

    // const std::string& studyInstanceUID = Orthanc::SerializationToolbox::ReadString(json, STUDY_INSTANCE_UID);
    // const std::string& seriesInstanceUID = Orthanc::SerializationToolbox::ReadString(json, SERIES_INSTANCE_UID);
    // const std::string& sopInstanceUID = Orthanc::SerializationToolbox::ReadString(json, SOP_INSTANCE_UID);

    std::string date, time;
    Orthanc::SystemToolbox::GetNowDicom(date, time, true);

    boost::filesystem::path path = SaolaConfiguration::Instance().GetMountDirectory();
    path /= "dicom";

    try
    {
      if (SaolaConfiguration::Instance().IsStoragePathFormatFull())
      {
        OrthancPlugins::OrthancString s;
        s.Assign(OrthancPluginDicomBufferToJson(OrthancPlugins::GetGlobalContext(), content, size,
                                                OrthancPluginDicomToJsonFormat_Short,
                                                OrthancPluginDicomToJsonFlags_None, 256));

        Json::Value json;
        s.ToJson(json);

        if (json.isMember(STUDY_DATE) && json[STUDY_DATE].type() == Json::stringValue)
        {
          std::string studyDate = Orthanc::SerializationToolbox::ReadString(json, STUDY_DATE);
          if (studyDate.size() >= date.size() && boost::regex_match(studyDate, REGEX_STUDY_DATE))
          {
            date = studyDate;
          }
          else
          {
            LOG(ERROR) << "[SaolaStorage][CreateMountDirectory] ERROR StudyDate " << studyDate << " is not standard";
          }
        }
        else
        {
          LOG(ERROR) << "[SaolaStorage][CreateMountDirectory] ERROR Cannot find tag StudyDate " << STUDY_DATE;
        }

        const std::string &studyInstanceUID = Orthanc::SerializationToolbox::ReadString(json, STUDY_INSTANCE_UID);
        const std::string &seriesInstanceUID = Orthanc::SerializationToolbox::ReadString(json, SERIES_INSTANCE_UID);

        path /= std::string(&date[0], &date[4]);
        path /= std::string(&date[4], &date[6]);
        path /= std::string(&date[6]);
        path /= studyInstanceUID;
        path /= seriesInstanceUID;
      }
      else
      {
        path /= std::string(&date[0], &date[4]);
        path /= std::string(&date[4], &date[6]);
        path /= std::string(&date[6]);
        path /= std::string(&uuid[0], &uuid[2]);
        path /= std::string(&uuid[2], &uuid[4]);
      }
    }
    catch (...)
    {
      LOG(ERROR) << "[SaolaStorage][CreateMountDirectory] ERROR Exception. Rollback to default configuration";
      path = SaolaConfiguration::Instance().GetMountDirectory();
      path /= "dicom";
      path /= std::string(&date[0], &date[4]);
      path /= std::string(&date[4], &date[6]);
      path /= std::string(&date[6]);
      path /= std::string(&uuid[0], &uuid[2]);
      path /= std::string(&uuid[2], &uuid[4]);
    }

    path /= uuid;
    path.make_preferred();
    return path;
  }

  return GetPathInternal(SaolaConfiguration::Instance().GetMountDirectory() + "/attachments", uuid);
}

static void CreateOrthancBuffer(OrthancPluginMemoryBuffer64 *target,
                                const std::string &content)
{
  OrthancPluginErrorCode code = OrthancPluginCreateMemoryBuffer64(
      OrthancPlugins::GetGlobalContext(), target, content.size());

  if (code == OrthancPluginErrorCode_Success)
  {
    assert(content.size() == target->size);

    if (!content.empty())
    {
      memcpy(target->data, content.c_str(), content.size());
    }
  }
  else
  {
    throw Orthanc::OrthancException(static_cast<Orthanc::ErrorCode>(code));
  }
}

void StorageArea::ReadWholeFromPath(OrthancPluginMemoryBuffer64 *target,
                                    const std::string &path)
{
  Orthanc::Toolbox::ElapsedTimer timer;
  LOG(INFO) << "SaolaStorageArea::ReadWholeFromPath path \"" << path << "\"";

  std::string content;
  Orthanc::SystemToolbox::ReadFile(content, path);
  CreateOrthancBuffer(target, content);

  LOG(INFO) << "SaolaStorageArea::ReadWholeFromPath path \"" << path << "\" (" << timer.GetHumanTransferSpeed(true, target->size) << ")";
}

void StorageArea::ReadRangeFromPath(OrthancPluginMemoryBuffer64 *target,
                                    const std::string &path,
                                    uint64_t rangeStart)
{
  Orthanc::Toolbox::ElapsedTimer timer;
  LOG(INFO) << "SaolaStorageArea::ReadRangeFromPath path \"" << path << "\" (range from: " << rangeStart << ")";

  std::string content;
  Orthanc::SystemToolbox::ReadFileRange(
      content, path, rangeStart, rangeStart + target->size, true);

  if (content.size() != target->size)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_CorruptedFile);
  }
  else if (!content.empty())
  {
    memcpy(target->data, content.c_str(), content.size());
  }

  LOG(INFO) << "SaolaStorageArea::ReadRangeFromPath path \"" << path << "\" (" << timer.GetHumanTransferSpeed(true, target->size) << ")";
}

StorageArea::StorageArea(const std::string &root) : root_(root)
{
  if (root_.empty())
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }

  {
    boost::filesystem::path root_path = boost::filesystem::absolute(root_);
    boost::filesystem::perms perms = boost::filesystem::status(root_path.parent_path()).permissions();
    if ((perms & boost::filesystem::perms::owner_read) == boost::filesystem::perms::no_perms ||
        (perms & boost::filesystem::perms::owner_write) == boost::filesystem::perms::no_perms)
    {
      LOG(ERROR) << "[SaolaStorageArea] ERROR StorageDirectory " << root_path << " is not accessible";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_CannotWriteFile);
    }
  }

  if (SaolaConfiguration::Instance().GetMountDirectory().empty())
  {
    LOG(ERROR) << "[SaolaStorageArea] ERROR MountDirectory should not be null or empty";
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }

  {
    boost::filesystem::path mount_path = boost::filesystem::absolute(SaolaConfiguration::Instance().GetMountDirectory());
    boost::filesystem::perms perms = boost::filesystem::status(mount_path.parent_path()).permissions();
    if ((perms & boost::filesystem::perms::owner_read) == boost::filesystem::perms::no_perms ||
        (perms & boost::filesystem::perms::owner_write) == boost::filesystem::perms::no_perms)
    {
      LOG(ERROR) << "[SaolaStorageArea] ERROR MountDirectory " << mount_path << " is not accessible";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_CannotWriteFile);
    }
  }
}

void StorageArea::Create(const std::string &uuid,
                         const void *content,
                         int64_t size)
{
  boost::filesystem::path root_path = GetPathInternal(root_, uuid);

  boost::filesystem::path mount_path = CreateMountDirectory(uuid, content, size);

  Orthanc::Toolbox::ElapsedTimer timer;
  LOG(INFO) << "SaolaStorageArea::Create creating attachment \"" << uuid << "\" to (root=" << root_path << ", mount_path=" << mount_path << ")";

  // In very unlikely cases, a thread could be deleting a
  // directory while another thread needs it -> introduce 3 retries at 1 ms interval
  int retryCount = 0;
  const int maxRetryCount = 3;

  while (retryCount < maxRetryCount)
  {
    retryCount++;
    if (retryCount > 1)
    {
      boost::this_thread::sleep(boost::posix_time::milliseconds(2 * retryCount + (rand() % 10)));
      LOG(INFO) << "Retrying (" << retryCount << ") to create attachment \"" << uuid << ", root=" << root_path << ", mount_path=" << mount_path;
    }

    // Validate root directory existence and Make root directory
    if (boost::filesystem::exists(root_path.parent_path()))
    {
      if (!boost::filesystem::is_directory(root_path.parent_path()))
      {
        LOG(ERROR) << "[SaolaStorageArea] ERROR Root directory: " << root_path.parent_path() << " is existed but not a directory";
        throw Orthanc::OrthancException(Orthanc::ErrorCode_DirectoryOverFile);
      }
    }
    else
    {
      try
      {
        boost::filesystem::create_directories(root_path.parent_path());
      }
      catch (boost::filesystem::filesystem_error &er)
      {
        if (er.code() == boost::system::errc::file_exists         // the last element of the parent_path is a file
            || er.code() == boost::system::errc::not_a_directory) // one of the element of the parent_path is not a directory
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_DirectoryOverFile,
                                          std::string("[SaolaStorageArea] Parent Root directory: ") + root_path.parent_path().c_str() + " is a file"); // no need to retry this error
        }
        // ignore other errors and retry
      }
    }

    // Validate mount directory existence and Make mount directory
    if (boost::filesystem::exists(mount_path.parent_path()))
    {
      if (!boost::filesystem::is_directory(mount_path.parent_path()))
      {
        LOG(ERROR) << "[SaolaStorageArea] ERROR Mount directory: " << mount_path.parent_path() << " is existed but not a directory";
        // throw Orthanc::OrthancException(Orthanc::ErrorCode_DirectoryOverFile);
      }
    }
    else
    {
      try
      {
        boost::filesystem::create_directories(mount_path.parent_path());
      }
      catch (boost::filesystem::filesystem_error &er)
      {
        if (er.code() == boost::system::errc::file_exists         // the last element of the parent_path is a file
            || er.code() == boost::system::errc::not_a_directory) // one of the element of the parent_path is not a directory
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_DirectoryOverFile,
                                          std::string("[SaolaStorageArea] ERROR Parent mount directory: ") + mount_path.parent_path().c_str() + " is a file"); // no need to retry this error
        }
        // ignore other errors and retry
      }
    }

    try
    {
      Orthanc::SystemToolbox::WriteFile(mount_path.string().c_str(), mount_path.string().size(), root_path.string() + EXTENSION, false);
      Orthanc::SystemToolbox::WriteFile(content, size, mount_path.string(), false);
      LOG(INFO) << "SaolaStorageArea::Create created attachment \"" << uuid << "\" (" << timer.GetHumanTransferSpeed(true, size) << ")";
      return;
    }
    catch (Orthanc::OrthancException &ex)
    {
      LOG(INFO) << "Writing file caught exception: " << ex.What();
      if (retryCount >= maxRetryCount)
      {
        throw ex;
      }
    }
  }
}

void StorageArea::ReadWhole(std::string &target,
                            const std::string &uuid)
{
  Orthanc::Toolbox::ElapsedTimer timer;
  LOG(INFO) << "SaolaStorageArea::ReadWhole string attachment \"" << uuid << "\"";

  boost::filesystem::path root_path = GetPathInternal(root_, uuid).string();
  std::string root_path_extension = root_path.string() + EXTENSION;
  if (Orthanc::SystemToolbox::IsExistingFile(root_path_extension))
  {
    std::string floc;
    Orthanc::SystemToolbox::ReadFile(floc, root_path_extension);
    Orthanc::SystemToolbox::ReadFile(target, floc);
  }
  else
  {
    Orthanc::SystemToolbox::ReadFile(target, root_path.c_str());
  }

  LOG(INFO) << "SaolaStorageArea::ReadWhole string attachment \"" << uuid << "\" (" << timer.GetHumanTransferSpeed(true, target.size()) << ")";
}

void StorageArea::ReadWhole(OrthancPluginMemoryBuffer64 *target,
                            const std::string &uuid)
{
  Orthanc::Toolbox::ElapsedTimer timer;
  LOG(INFO) << "SaolaStorageArea::ReadWhole OrthancPluginMemoryBuffer64 attachment \"" << uuid << "\"";

  boost::filesystem::path root_path = GetPathInternal(root_, uuid).string();
  std::string root_path_extension = root_path.string() + EXTENSION;
  if (Orthanc::SystemToolbox::IsExistingFile(root_path_extension))
  {
    std::string floc;
    Orthanc::SystemToolbox::ReadFile(floc, root_path_extension);
    ReadWholeFromPath(target, floc);
  }
  else
  {
    ReadWholeFromPath(target, root_path.string());
  }

  LOG(INFO) << "SaolaStorageArea::ReadWhole OrthancPluginMemoryBuffer64 attachment \"" << uuid << "\" (" << timer.GetHumanTransferSpeed(true, target->size) << ")";
}

void StorageArea::ReadRange(OrthancPluginMemoryBuffer64 *target,
                            const std::string &uuid,
                            uint64_t rangeStart)
{
  Orthanc::Toolbox::ElapsedTimer timer;
  LOG(INFO) << "SaolaStorageArea::ReadRange attachment \"" << uuid << "\" content type (range from: " << rangeStart << ")";

  boost::filesystem::path root_path = GetPathInternal(root_, uuid).string();
  std::string root_path_extension = root_path.string() + EXTENSION;
  if (Orthanc::SystemToolbox::IsExistingFile(root_path_extension))
  {
    std::string floc;
    Orthanc::SystemToolbox::ReadFile(floc, root_path_extension);
    ReadRangeFromPath(target, floc, rangeStart);
  }
  else
  {
    ReadRangeFromPath(target, root_path.string(), rangeStart);
  }

  LOG(INFO) << "SaolaStorageArea::ReadRange attachment \"" << uuid << "\" (" << timer.GetHumanTransferSpeed(true, target->size) << ")";
}

void StorageArea::RemoveAttachment(const std::string &uuid)
{
  Orthanc::Toolbox::ElapsedTimer timer;
  LOG(INFO) << "SaolaStorageArea::RemoveAttachment deleting attachment \"" << uuid << "\"";

  boost::filesystem::path root_path = GetPathInternal(root_, uuid);

  try
  {
    if (Orthanc::SystemToolbox::IsExistingFile(root_path.string() + EXTENSION))
    {
      LOG(INFO) << "SaolaStorageArea::RemoveAttachment Found and Deleting symlink file " << root_path.string() + EXTENSION;
      root_path.concat(EXTENSION);

      std::string floc;
      Orthanc::SystemToolbox::ReadFile(floc, root_path.string());
      boost::filesystem::path mount_path = floc;

      boost::system::error_code err;
      boost::filesystem::remove(root_path, err);
      boost::filesystem::remove(root_path.parent_path(), err);
      boost::filesystem::remove(root_path.parent_path().parent_path(), err);

      LOG(INFO) << "SaolaStorageArea::RemoveAttachment Found and Deleting mount file " << mount_path;
      boost::filesystem::remove(mount_path, err);
      boost::filesystem::remove(mount_path.parent_path(), err);
      boost::filesystem::remove(mount_path.parent_path().parent_path(), err);
    }
    else
    {
      LOG(INFO) << "SaolaStorageArea::RemoveAttachment Deleting regular file " << root_path.string() + EXTENSION;
      boost::system::error_code err;
      boost::filesystem::remove(root_path, err);
      boost::filesystem::remove(root_path.parent_path(), err);
      boost::filesystem::remove(root_path.parent_path().parent_path(), err);
    }
  }
  catch (...)
  {
    // Ignore the error
  }

  LOG(INFO) << "SaolaStorageArea::RemoveAttachment deleted attachment \"" << uuid << "\" (" << timer.GetHumanElapsedDuration() << ")";
}

std::string StorageArea::GetPath(const std::string &uuid) const
{
  return GetPathInternal(SaolaConfiguration::Instance().GetMountDirectory(), uuid).string();
}
