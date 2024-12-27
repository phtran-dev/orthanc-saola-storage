#include "DeletionWorker.h"

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Logging.h>
#include <Enumerations.h>
#include <chrono>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

namespace Saola
{
  static const char *DELAYED_DELETION = "DelayedDeletion";

  void DeletionWorker::Run()
  {
    static const unsigned int GRANULARITY = 100; // In milliseconds

    std::string uuid;
    Orthanc::FileContentType type = Orthanc::FileContentType_Dicom; // Dummy initialization

    bool hasDeleted = false;

    while (this->m_state == State_Running && db_->Dequeue(uuid, type))
    {
      if (!hasDeleted)
      {
        LOG(INFO) << "DelayedDeletion - Starting to process the pending deletions";
      }

      hasDeleted = true;

      try
      {
        LOG(INFO) << "DelayedDeletion - Asynchronous removal of file: " << uuid << "\" of type " << static_cast<int>(type);
        storageArea_->RemoveAttachment(uuid);
        int throttleDelayMs_ = 0;
        if (throttleDelayMs_ > 0)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(throttleDelayMs_));
        }
      }
      catch (Orthanc::OrthancException &ex)
      {
        LOG(ERROR) << "DelayedDeletion - Cannot remove file: " << uuid << " " << ex.What();
      }
    }

    if (hasDeleted)
    {
      LOG(INFO) << "DelayedDeletion - All the pending deletions have been completed";
    }
  }

  void DeletionWorker::Start()
  {
    LOG(WARNING) << "DelayedDeletion - Starting the deletion thread";
    if (this->m_state != State_Setup)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }

    this->m_state = State_Running;
    const auto intervalSeconds = 10;
    this->m_worker = new std::thread([this, intervalSeconds]()
                                     {
    while (this->m_state == State_Running)
    {
      this->Run();
      for (unsigned int i = 0; i < intervalSeconds * 10; i++)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } });
  }

  void DeletionWorker::Stop()
  {
    LOG(WARNING) << "DelayedDeletion - Stopping the deletion thread";
    if (this->m_state == State_Running)
    {
      this->m_state = State_Done;
      if (this->m_worker->joinable())
        this->m_worker->join();
      delete this->m_worker;
    }
  }

  void DeletionWorker::GetStatistics(Json::Value &status)
  {
    status["FilesPendingDeletion"] = db_->GetSize();
    status["DatabaseServerIdentifier"] = databaseServerIdentifier_;
  }

  void DeletionWorker::Enqueue(const std::string& uuid, Orthanc::FileContentType type)
  {
    LOG(INFO) << "DelayedDeletion - Scheduling delayed deletion of " << uuid;
    db_->Enqueue(uuid, type);
  }

  DeletionWorker::DeletionWorker(std::shared_ptr<StorageArea> &storageArea)
      : storageArea_(storageArea), m_state(State_Setup)
  {
    OrthancPlugins::OrthancConfiguration orthancConfig, delayedDeletionConfig;
    orthancConfig.GetSection(delayedDeletionConfig, DELAYED_DELETION);

    databaseServerIdentifier_ = OrthancPluginGetDatabaseServerIdentifier(OrthancPlugins::GetGlobalContext());
    throttleDelayMs_ = delayedDeletionConfig.GetUnsignedIntegerValue("ThrottleDelayMs", 0); // delay in ms

    std::string pathStorage = orthancConfig.GetStringValue("StorageDirectory", "OrthancStorage");
    LOG(WARNING) << "DelayedDeletion - Path to the storage area: " << pathStorage;

    boost::filesystem::path defaultDbPath = boost::filesystem::path(pathStorage) / (std::string("pending-deletions.") + databaseServerIdentifier_ + ".db");
    std::string dbPath = delayedDeletionConfig.GetStringValue("Path", defaultDbPath.string());

    LOG(WARNING) << "DelayedDeletion - Path to the SQLite database: " << dbPath;

    db_.reset(new Saola::PendingDeletionsDatabase(dbPath));
  }

  DeletionWorker::~DeletionWorker()
  {
    if (this->m_state == State_Running)
    {
      OrthancPlugins::LogError("StableEventScheduler::Stop() should have been manually called");
      Stop();
    }
  }

}
