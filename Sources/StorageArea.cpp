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

static boost::filesystem::path GetPathInternal(const std::string& root,
                                               const std::string& uuid)
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


static void CreateOrthancBuffer(OrthancPluginMemoryBuffer64 *target,
                                const std::string& content)
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
                                    const std::string& path)
{
  std::string content;
  Orthanc::SystemToolbox::ReadFile(content, path);
  CreateOrthancBuffer(target, content);
}   
  

void StorageArea::ReadRangeFromPath(OrthancPluginMemoryBuffer64 *target,
                                    const std::string& path,
                                    uint64_t rangeStart)
{
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
}


StorageArea::StorageArea(const std::string& root, const std::string& mount) :
  root_(root), mount_(mount)
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
      LOG(ERROR) << "[SaolaStorageArea] StorageDirectory " << root_path << " is not accessible" ;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_CannotWriteFile);
    }
  }
      
  if (mount_.empty())
  {
    LOG(ERROR) << "[SaolaStorageArea] MountDirectory should not bet null or empty";
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }

  {
    boost::filesystem::path mount_path = boost::filesystem::absolute(mount_);
    boost::filesystem::perms perms = boost::filesystem::status(mount_path.parent_path()).permissions();
    if ((perms & boost::filesystem::perms::owner_read) == boost::filesystem::perms::no_perms ||
        (perms & boost::filesystem::perms::owner_write) == boost::filesystem::perms::no_perms)
    {
      LOG(ERROR) << "[SaolaStorageArea] MountDirectory " << mount_path << " is not accessible" ;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_CannotWriteFile);
    }
  }
}

static boost::filesystem::path CreateMountDirectory(const std::string& mount,
                                                    const std::string& uuid,
                                                    const void *content,
                                                    int64_t size)
{
  if (!Orthanc::Toolbox::IsUuid(uuid))
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }
  
  assert(!mount.empty());
      
  boost::filesystem::path path = mount;

  if (size > 0 && Orthanc::DicomMap::IsDicomFile(content, size))
  {
    OrthancPlugins::OrthancString s;
    s.Assign(OrthancPluginDicomBufferToJson(OrthancPlugins::GetGlobalContext(), content, size,
                                            OrthancPluginDicomToJsonFormat_Short,
                                            OrthancPluginDicomToJsonFlags_None, 256));
  
    Json::Value json;
    s.ToJson(json);

    static const char* const PATIENT_ID = "0010,0020";
    static const char* const STUDY_INSTANCE_UID = "0020,000d";
    static const char* const SERIES_INSTANCE_UID = "0020,000e";
    static const char* const SOP_INSTANCE_UID = "0008,0018";

    const std::string& studyInstanceUID = Orthanc::SerializationToolbox::ReadString(json, STUDY_INSTANCE_UID);
    const std::string& seriesInstanceUID = Orthanc::SerializationToolbox::ReadString(json, SERIES_INSTANCE_UID);
    const std::string& sopInstanceUID = Orthanc::SerializationToolbox::ReadString(json, SOP_INSTANCE_UID);

    path /= "dicom";
    path /= studyInstanceUID;
    path /= seriesInstanceUID;
    // path /= sopInstanceUID;
    path /= uuid;
    path.make_preferred();
    return path;
  }

  path /= "attachments";
  path /= std::string(&uuid[0], &uuid[2]);
  path /= std::string(&uuid[2], &uuid[4]);
  path /= uuid;
  path.make_preferred();
  return path;
}
  
void StorageArea::Create(const std::string& uuid,
                         const void *content,
                         int64_t size)
{
  boost::filesystem::path root_path = GetPathInternal(root_, uuid);

  // boost::filesystem::path mount_path = GetPathInternal(mount_, uuid);
  boost::filesystem::path mount_path = CreateMountDirectory(mount_, uuid, content, size);
  

  // Validate root directory existence and Make root directory
  if (boost::filesystem::exists(root_path.parent_path()))
  {
    if (!boost::filesystem::is_directory(root_path.parent_path()))
    {
      LOG(ERROR) << "[SaolaStorageArea] Root directory: " << root_path.parent_path() << " is existed but not a directory";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_DirectoryOverFile);
    }
  }
  else
  {
    if (!boost::filesystem::create_directories(root_path.parent_path()))
    {
      LOG(ERROR) << "[SaolaStorageArea] Root directory: " << root_path.parent_path() << " cannot be created";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_FileStorageCannotWrite);
    }
  }

  // Validate mount directory existence and Make mount directory
  if (boost::filesystem::exists(mount_path.parent_path()))
  {
    if (!boost::filesystem::is_directory(mount_path.parent_path()))
    {
      LOG(ERROR) << "[SaolaStorageArea] Mount directory: " << mount_path.parent_path() << " is existed but not a directory";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_DirectoryOverFile);
    }
  }
  else
  {
    if (!boost::filesystem::create_directories(mount_path.parent_path()))
    {
      LOG(ERROR) << "[SaolaStorageArea] Mount directory: " << mount_path.parent_path() << " cannot be created";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_FileStorageCannotWrite);
    }
  }

  Orthanc::SystemToolbox::WriteFile(mount_path.string().c_str(), mount_path.string().size(), root_path.string(), false);

  Orthanc::SystemToolbox::WriteFile(content, size, mount_path.string(), false);

  boost::system::error_code se;

  // boost::filesystem::create_symlink(realpath(path.c_str(), NULL), sym_path);
  // boost::filesystem::create_symlink(mount_path, relative, se);
  // if (se.failed())
  // {
  //   LOG(ERROR) << "[ItechStorageArea] Cannot create symlink from " << mount_path << " to " << relative;
  //   throw Orthanc::OrthancException(Orthanc::ErrorCode_FileStorageCannotWrite);
  // }

  // auto relative = boost::filesystem::relative(mount_path.parent_path(), root_path.parent_path());
  // boost::filesystem::create_symlink(relative /= uuid , root_path, se);
  // if (se.failed())
  // {
  //   LOG(ERROR) << "[ItechStorageArea] Cannot create symlink from " << mount_path << " to " << relative;
  //   throw Orthanc::OrthancException(Orthanc::ErrorCode_FileStorageCannotWrite);
  // }

}


void StorageArea::ReadWhole(std::string& target,
                            const std::string& uuid)
{
  std::string floc;
  Orthanc::SystemToolbox::ReadFile(floc, GetPathInternal(root_, uuid).string());
  Orthanc::SystemToolbox::ReadFile(target, floc);
}
  

void StorageArea::ReadWhole(OrthancPluginMemoryBuffer64 *target,
                            const std::string& uuid)
{
  std::string floc;
  Orthanc::SystemToolbox::ReadFile(floc, GetPathInternal(root_, uuid).string());

  LOG(INFO) << "[SaolaStorageArea] PHONG ReadWhole2 file path= " << floc;
  ReadWholeFromPath(target, floc);
}
  

void StorageArea::ReadRange(OrthancPluginMemoryBuffer64 *target,
                            const std::string& uuid,
                            uint64_t rangeStart)
{

  std::string floc;
  Orthanc::SystemToolbox::ReadFile(floc, GetPathInternal(root_, uuid).string());
  ReadRangeFromPath(target, floc, rangeStart);
}
  

void StorageArea::RemoveAttachment(const std::string& uuid)
{
  boost::filesystem::path root_path = GetPathInternal(root_, uuid);
  boost::filesystem::path mount_path = GetPathInternal(mount_, uuid);
      
  try
  {
    boost::system::error_code err;
    boost::filesystem::remove(root_path, err);
    boost::filesystem::remove(root_path.parent_path(), err);
    boost::filesystem::remove(root_path.parent_path().parent_path(), err);

    boost::filesystem::remove(mount_path, err);
    boost::filesystem::remove(mount_path.parent_path(), err);
    boost::filesystem::remove(mount_path.parent_path().parent_path(), err);
  }
  catch (...)
  {
    // Ignore the error
  }
}


std::string StorageArea::GetPath(const std::string& uuid) const
{
  return GetPathInternal(mount_, uuid).string();
}
