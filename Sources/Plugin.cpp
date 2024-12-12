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

#include <Logging.h>

#include <boost/filesystem.hpp>

static std::unique_ptr<StorageArea> storageArea_;

static const char *const ORTHANC_STORAGE = "OrthancStorage";

static const char *const STORAGE_DIRECTORY = "StorageDirectory";

static const char *const MOUNT_DIRECTORY = "MountDirectory";

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
    storageArea_->RemoveAttachment(uuid);
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

    OrthancPluginSetDescription(context, "Synchronize Orthanc with directories containing DICOM files.");

    if (SaolaConfiguration::Instance().IsEnabled())
    {
      try
      {
        OrthancPlugins::OrthancConfiguration configuration;
        storageArea_.reset(new StorageArea(configuration.GetStringValue(STORAGE_DIRECTORY, ORTHANC_STORAGE)));
      }
      catch (Orthanc::OrthancException &e)
      {
        return -1;
      }
      catch (...)
      {
        LOG(ERROR) << "Native exception while initializing the plugin";
        return -1;
      }

      OrthancPluginRegisterStorageArea2(context, StorageCreate, StorageReadWhole, StorageReadRange, StorageRemove);
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
    return "OrthancSaolaStorage";
  }

  ORTHANC_PLUGINS_API const char *OrthancPluginGetVersion()
  {
    return ORTHANC_PLUGIN_VERSION;
  }
}
