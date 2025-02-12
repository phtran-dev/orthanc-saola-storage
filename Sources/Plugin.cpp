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
#include "PendingDeletionsDatabase.h"
#include "DeletionWorker.h"

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Logging.h>
#include <Toolbox.h>
#include <Enumerations.h>
#include <orthanc/OrthancCPlugin.h>
#include <IDynamicObject.h>
#include <DicomFormat/DicomInstanceHasher.h>
#include <SerializationToolbox.h>
#include <SystemToolbox.h>

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

static std::shared_ptr<StorageArea> storageArea_;

static const char *const SAOLA_STORAGE = "SaolaStorage";

static const char *const ORTHANC_STORAGE = "OrthancStorage";

static const char *const STORAGE_DIRECTORY = "StorageDirectory";

static const char *const MOUNT_DIRECTORY = "MountDirectory";

class PendingDeletion : public Orthanc::IDynamicObject
{
private:
  Orthanc::FileContentType type_;
  std::string uuid_;

public:
  PendingDeletion(Orthanc::FileContentType type,
                  const std::string &uuid) : type_(type),
                                             uuid_(uuid)
  {
  }

  Orthanc::FileContentType GetType() const
  {
    return type_;
  }

  const std::string &GetUuid() const
  {
    return uuid_;
  }
};

static std::unique_ptr<Saola::DeletionWorker> deletionWorker_;

static Orthanc::FileContentType Convert(OrthancPluginContentType type)
{
  switch (type)
  {
  case OrthancPluginContentType_Dicom:
    return Orthanc::FileContentType_Dicom;

  case OrthancPluginContentType_DicomAsJson:
    return Orthanc::FileContentType_DicomAsJson;

  case OrthancPluginContentType_DicomUntilPixelData:
    return Orthanc::FileContentType_DicomUntilPixelData;

  default:
    return Orthanc::FileContentType_Unknown;
  }
}

OrthancPluginErrorCode OnChangeCallback(OrthancPluginChangeType changeType,
                                        OrthancPluginResourceType resourceType,
                                        const char *resourceId)
{
  switch (changeType)
  {
  case OrthancPluginChangeType_OrthancStarted:
    if (SaolaConfiguration::Instance().DelayedDeletionEnable())
    {
      deletionWorker_.reset(new Saola::DeletionWorker(storageArea_));
      deletionWorker_->Start();
    }

    break;

  case OrthancPluginChangeType_OrthancStopped:
    if (deletionWorker_.get() != NULL)
    {
      deletionWorker_->Stop();
    }

    break;

  default:
    break;
  }

  return OrthancPluginErrorCode_Success;
}

void GetPluginConfiguration(OrthancPluginRestOutput *output,
                            const char *url,
                            const OrthancPluginHttpRequest *request)
{
  const std::string &s = SaolaConfiguration::Instance().ToJsonString();
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(),
                            s.size(), "application/json");
}

void ApplyPluginConfiguration(OrthancPluginRestOutput *output,
                              const char *url,
                              const OrthancPluginHttpRequest *request)
{
  Json::Value target;
  Orthanc::Toolbox::ReadJsonWithoutComments(target, request->body, request->bodySize);
  OrthancPlugins::OrthancConfiguration config(target, "SaolaStorage"), saolaSection;
  if (!config.IsSection(SAOLA_STORAGE))
  {
    return OrthancPluginSendHttpStatusCode(OrthancPlugins::GetGlobalContext(), output, 400);
  }

  config.GetSection(saolaSection, SAOLA_STORAGE);
  SaolaConfiguration::Instance().ApplyConfiguration(saolaSection.GetJson());

  const std::string &s = SaolaConfiguration::Instance().ToJsonString();
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(),
                            s.size(), "application/json");
}

void GetPluginStatus(OrthancPluginRestOutput *output,
                     const char *url,
                     const OrthancPluginHttpRequest *request)
{

  Json::Value status;
  if (deletionWorker_.get() != NULL)
  {
    deletionWorker_->GetStatistics(status);
  }

  std::string s = status.toStyledString();
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(),
                            s.size(), "application/json");
}

static OrthancPluginErrorCode StorageCreate(const char *uuid,
                                            const void *content,
                                            int64_t size,
                                            OrthancPluginContentType type)
{
  try
  {
    storageArea_->Create(uuid, content, size);
    return OrthancPluginErrorCode_Success;
  }
  catch (Orthanc::OrthancException &e)
  {
    LOG(ERROR) << e.What();
    return static_cast<OrthancPluginErrorCode>(e.GetErrorCode());
  }
  catch (...)
  {
    return OrthancPluginErrorCode_InternalError;
  }
}

static OrthancPluginErrorCode StorageReadRange(OrthancPluginMemoryBuffer64 *target,
                                               const char *uuid,
                                               OrthancPluginContentType type,
                                               uint64_t rangeStart)
{
  try
  {
    storageArea_->ReadRange(target, uuid, rangeStart);
    return OrthancPluginErrorCode_Success;
  }
  catch (Orthanc::OrthancException &e)
  {
    LOG(ERROR) << e.What();
    return static_cast<OrthancPluginErrorCode>(e.GetErrorCode());
  }
  catch (...)
  {
    return OrthancPluginErrorCode_InternalError;
  }
}

static OrthancPluginErrorCode StorageReadWhole(OrthancPluginMemoryBuffer64 *target,
                                               const char *uuid,
                                               OrthancPluginContentType type)
{
  try
  {
    storageArea_->ReadWhole(target, uuid);
    return OrthancPluginErrorCode_Success;
  }
  catch (Orthanc::OrthancException &e)
  {
    LOG(ERROR) << e.What();
    return static_cast<OrthancPluginErrorCode>(e.GetErrorCode());
  }
  catch (...)
  {
    return OrthancPluginErrorCode_InternalError;
  }
}

static OrthancPluginErrorCode StorageRemove(const char *uuid,
                                            OrthancPluginContentType type)
{
  try
  {
    // Delayed Deletion enabled
    if (deletionWorker_.get() != NULL)
    {
      deletionWorker_->Enqueue(uuid, Convert(type));
    }
    else
    {
      storageArea_->RemoveAttachment(uuid);
    }
    return OrthancPluginErrorCode_Success;
  }
  catch (Orthanc::OrthancException &e)
  {
    LOG(ERROR) << e.What();
    return static_cast<OrthancPluginErrorCode>(e.GetErrorCode());
  }
  catch (...)
  {
    return OrthancPluginErrorCode_InternalError;
  }
}

OrthancPluginReceivedInstanceAction ReceivedInstanceCallback(OrthancPluginMemoryBuffer64 *modifiedDicomBuffer,
                                                             const void *receivedDicomBuffer,
                                                             uint64_t receivedDicomBufferSize,
                                                             OrthancPluginInstanceOrigin origin)
{
  {
    OrthancPlugins::OrthancString s;
    s.Assign(OrthancPluginDicomBufferToJson(OrthancPlugins::GetGlobalContext(), receivedDicomBuffer, receivedDicomBufferSize,
                                            OrthancPluginDicomToJsonFormat_Short,
                                            OrthancPluginDicomToJsonFlags_None, 256));

    Json::Value json;
    s.ToJson(json);

    LOG(INFO) << "PHONG ReceivedInstanceCallback json=" << json.toStyledString();

    static const char *const PATIENT_ID = "0010,0020";
    static const char *const STUDY_INSTANCE_UID = "0020,000d";
    static const char *const SERIES_INSTANCE_UID = "0020,000e";
    static const char *const SOP_INSTANCE_UID = "0008,0018";

    // Orthanc::DicomInstanceHasher hasher(
    //   json.isMember(PATIENT_ID) ? Orthanc::SerializationToolbox::ReadString(json, PATIENT_ID) : "",
    //   Orthanc::SerializationToolbox::ReadString(json, STUDY_INSTANCE_UID),
    //   Orthanc::SerializationToolbox::ReadString(json, SERIES_INSTANCE_UID),
    //   Orthanc::SerializationToolbox::ReadString(json, SOP_INSTANCE_UID));

    // instanceId = hasher.HashInstance();
    // return true;
  }

  memcpy(modifiedDicomBuffer->data, receivedDicomBuffer, receivedDicomBufferSize);

  return OrthancPluginReceivedInstanceAction_Modify;
}

static int32_t FilterIncomingDicomInstance(const OrthancPluginDicomInstance *instance)
{
  OrthancPlugins::OrthancString s;
  s.Assign(OrthancPluginGetInstanceJson(OrthancPlugins::GetGlobalContext(), instance));

  Json::Value json;
  s.ToJson(json);

  static const char *const PATIENT_ID = "0010,0020";
  static const char *const STUDY_INSTANCE_UID = "0020,000d";
  static const char *const SERIES_INSTANCE_UID = "0020,000e";
  static const char *const SOP_INSTANCE_UID = "0008,0018";
  static const char *const VALUE = "Value";

  const std::string& studyInstanceUID = Orthanc::SerializationToolbox::ReadString(json[STUDY_INSTANCE_UID], VALUE);
  const std::string& seriesInstanceUID = Orthanc::SerializationToolbox::ReadString(json[STUDY_INSTANCE_UID], VALUE);
  const std::string& sopInstanceUID = Orthanc::SerializationToolbox::ReadString(json[SOP_INSTANCE_UID], VALUE);

  Orthanc::DicomInstanceHasher hasher(
      json.isMember(PATIENT_ID) ? Orthanc::SerializationToolbox::ReadString(json[PATIENT_ID], VALUE) : "",
      Orthanc::SerializationToolbox::ReadString(json[STUDY_INSTANCE_UID], VALUE),
      Orthanc::SerializationToolbox::ReadString(json[SERIES_INSTANCE_UID], VALUE),
      Orthanc::SerializationToolbox::ReadString(json[SOP_INSTANCE_UID], VALUE));

  const std::string &instanceId = hasher.HashInstance();

  Json::Value stats;
  if (OrthancPlugins::RestApiGet(stats, "/instances/" + instanceId + "/statistics", false) && !stats.isNull() && !stats.empty())
  {
    /* Reject instance if already existing */
    LOG(INFO) << "[OrthancStorage] FilterIncomingDicomInstance InstanceID already existing: " << instanceId << " , discard incomming StudyInstanceUID " << studyInstanceUID
              << ", SeriesInstanceUID " << seriesInstanceUID << ", SOPInstanceUID " << sopInstanceUID;
    return 0;
  }

  return 1;
}

extern "C"
{
  ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext *context)
  {
    OrthancPlugins::SetGlobalContext(context);
    Orthanc::Logging::InitializePluginContext(context);
    Orthanc::Logging::EnableInfoLevel(true);

    /* Check the version of the Orthanc core */
    if (OrthancPluginCheckVersion(context) == 0)
    {
      OrthancPlugins::ReportMinimalOrthancVersion(ORTHANC_PLUGINS_MINIMAL_MAJOR_NUMBER,
                                                  ORTHANC_PLUGINS_MINIMAL_MINOR_NUMBER,
                                                  ORTHANC_PLUGINS_MINIMAL_REVISION_NUMBER);
      return -1;
    }

    OrthancPluginSetDescription(context, "Implementation of OrthancStorage with supporting multiple storage directories");

    if (SaolaConfiguration::Instance().IsEnabled())
    {
      try
      {
        OrthancPlugins::OrthancConfiguration orthancConfig;

        storageArea_.reset(new StorageArea(orthancConfig.GetStringValue(STORAGE_DIRECTORY, ORTHANC_STORAGE)));
      }
      catch (Orthanc::OrthancException &e)
      {
        LOG(ERROR) << "[SaolaStorage] ERROR Initialization OrthancException " << e.What();
        return -1;
      }
      catch (...)
      {
        LOG(ERROR) << "[SaolaStorage] ERROR Native exception while initializing the plugin";
        return -1;
      }
      // OrthancPluginRegisterReceivedInstanceCallback(context, ReceivedInstanceCallback);
      if (SaolaConfiguration::Instance().FilterIncomingDicomInstance())
      {
        OrthancPluginRegisterIncomingDicomInstanceFilter(context, FilterIncomingDicomInstance);
      }

      OrthancPluginRegisterStorageArea2(context, StorageCreate, StorageReadWhole, StorageReadRange, StorageRemove);
      OrthancPluginRegisterOnChangeCallback(context, OnChangeCallback);
      OrthancPlugins::RegisterRestCallback<GetPluginConfiguration>(SaolaConfiguration::Instance().GetRoot() + ORTHANC_PLUGIN_NAME + "/configuration", true);
      OrthancPlugins::RegisterRestCallback<ApplyPluginConfiguration>(SaolaConfiguration::Instance().GetRoot() + ORTHANC_PLUGIN_NAME + "/configuration/apply", true);
      OrthancPlugins::RegisterRestCallback<GetPluginStatus>(SaolaConfiguration::Instance().GetRoot() + ORTHANC_PLUGIN_NAME + "/delayed-deletion/status", true);
    }
    else
    {
      OrthancPlugins::LogWarning("OrthancSaolaStorage is disabled");
    }

    return 0;
  }

  ORTHANC_PLUGINS_API void OrthancPluginFinalize()
  {
    OrthancPlugins::LogWarning("OrthancSaolaStorage plugin is finalizing");
  }

  ORTHANC_PLUGINS_API const char *OrthancPluginGetName()
  {
    return ORTHANC_PLUGIN_NAME;
  }

  ORTHANC_PLUGINS_API const char *OrthancPluginGetVersion()
  {
    return ORTHANC_PLUGIN_VERSION;
  }
}
