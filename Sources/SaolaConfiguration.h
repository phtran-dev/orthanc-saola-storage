#pragma once

#include <json/value.h>
#include <string>

class SaolaConfiguration
{
private:

  bool enable_;

  int maxRetry_ = 5;

  std::string storagePathFormat_;

  std::string root_;

  std::string mountDirectory_;

  bool delayedDeletionEnable_;

  bool filterIncomingDicomInstance_;

  int delayedDeletionThrottleDelayMs_ = 0;

  std::string delayedDeletionPath_;

  SaolaConfiguration(/* args */);

public:

  static SaolaConfiguration& Instance();

  bool IsEnabled() const;

  bool IsStoragePathFormatFull() const;

  const std::string& GetRoot() const;

  const std::string& GetMountDirectory() const;

  bool DelayedDeletionEnable() const;

  bool FilterIncomingDicomInstance() const;

  int DelayedDeletionThrottleDelayMs() const;

  const std::string& DelayedDeletionPath() const;

  void ApplyConfiguration(const Json::Value& config);

  void ToJson(Json::Value& value) const;

  const std::string ToJsonString() const;

};