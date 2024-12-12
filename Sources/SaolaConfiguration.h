#pragma once

#include <string>

class SaolaConfiguration
{
private:

  bool enable_;

  int maxRetry_ = 5;

  bool is_storage_path_format_full_ = false;

  std::string mount_directory_;

  SaolaConfiguration(/* args */);

public:

  static SaolaConfiguration& Instance();

  bool IsEnabled() const;

  bool IsStoragePathFormatFull() const;

  const std::string& GetMountDirectory() const;

};