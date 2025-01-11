#pragma once

#include <string>

class SaolaConfiguration
{
private:

  bool enable_;

  int maxRetry_ = 5;

  bool is_storage_path_format_full_ = false;

  std::string root_;

  std::string mount_directory_;

  bool delayedDeletionEnable_;

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

  int DelayedDeletionThrottleDelayMs() const;

  const std::string& DelayedDeletionPath() const;

};