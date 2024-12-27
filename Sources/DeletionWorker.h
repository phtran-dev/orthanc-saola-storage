#pragma once

#include "PendingDeletionsDatabase.h"
#include "StorageArea.h"

#include <thread>
#include <boost/noncopyable.hpp>
#include <json/value.h>

namespace Saola
{
  class DeletionWorker
  {
  private:
    enum State
    {
      State_Setup,
      State_Running,
      State_Done
    };

    std::string databaseServerIdentifier_;

    int throttleDelayMs_;

    std::thread *m_worker;

    std::unique_ptr<Saola::PendingDeletionsDatabase> db_;

    std::shared_ptr<StorageArea> storageArea_;

    State m_state;

    void Run();

  public:

    DeletionWorker(std::shared_ptr<StorageArea>& storageArea);

    ~DeletionWorker();

    void GetStatistics(Json::Value& status);

    void Enqueue(const std::string& uuid, Orthanc::FileContentType type);

    void Start();

    void Stop();
  };
}
