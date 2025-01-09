#include "DeletionWorker.h"
#include "SaolaConfiguration.h"

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Logging.h>
#include <Enumerations.h>
#include <chrono>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

namespace Saola
{
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
        LOG(INFO) << "[SaolaStorage][DelayedDeletion] - Starting to process the pending deletions";
      }

      hasDeleted = true;

      try
      {
        LOG(INFO) << "[SaolaStorage][DelayedDeletion] - Asynchronous removal of file: " << uuid << "\" of type " << static_cast<int>(type);
        storageArea_->RemoveAttachment(uuid);
        if (SaolaConfiguration::Instance().DelayedDeletionThrottleDelayMs() > 0)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(SaolaConfiguration::Instance().DelayedDeletionThrottleDelayMs()));
        }
      }
      catch (Orthanc::OrthancException &ex)
      {
        LOG(ERROR) << "[SaolaStorage][DelayedDeletion] - Cannot remove file: " << uuid << " " << ex.What();
      }
    }

    if (hasDeleted)
    {
      LOG(INFO) << "[SaolaStorage][DelayedDeletion] - All the pending deletions have been completed";
    }
  }

  void DeletionWorker::Start()
  {
    LOG(WARNING) << "[SaolaStorage][DelayedDeletion] - Starting the deletion thread";
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
    LOG(WARNING) << "[SaolaStorage][DelayedDeletion] - Stopping the deletion thread";
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
    LOG(INFO) << "[SaolaStorage][DelayedDeletion] - Scheduling delayed deletion of " << uuid;
    db_->Enqueue(uuid, type);
  }

  DeletionWorker::DeletionWorker(std::shared_ptr<StorageArea> &storageArea)
      : storageArea_(storageArea), m_state(State_Setup)
  {
    databaseServerIdentifier_ = OrthancPluginGetDatabaseServerIdentifier(OrthancPlugins::GetGlobalContext());

    LOG(WARNING) << "[SaolaStorage][DelayedDeletion] - Path to the storage area: " << SaolaConfiguration::Instance().DelayedDeletionPath();

    boost::filesystem::path defaultDbPath = boost::filesystem::path(SaolaConfiguration::Instance().DelayedDeletionPath()) / (std::string("saola-storage-pending-deletions.") + databaseServerIdentifier_ + ".db");
    std::string dbPath = defaultDbPath.string();

    LOG(WARNING) << "[SaolaStorage][DelayedDeletion] - Path to the SQLite database: " << dbPath;

    db_.reset(new Saola::PendingDeletionsDatabase(dbPath));
  }

  DeletionWorker::~DeletionWorker()
  {
    if (this->m_state == State_Running)
    {
      OrthancPlugins::LogError("[SaolaStorage][DelayedDeletion]::Stop() should have been manually called");
      Stop();
    }
  }

}
