#include "spn.h"

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  DIR* dir = opendir("/source/H");
  if (!dir) {
    return 1;
  }
  mkdir("/work/G", 0755);
  struct dirent* entry = NULL;
  while ((entry = readdir(dir))) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    char from [256] = { 0 };
    char to [256] = { 0 };
    char content [64] = { 0 };
    snprintf(from, sizeof(from), "/source/H/%s", entry->d_name);
    snprintf(to, sizeof(to), "/work/G/%s", entry->d_name);
    FILE* file = fopen(from, "r");
    if (!file) {
      return 1;
    }
    fread(content, 1, sizeof(content) - 1, file);
    fclose(file);
    file = fopen(to, "w");
    if (!file) {
      return 1;
    }
    fputs(content, file);
    fclose(file);
  }
  closedir(dir);
  return 0;
}
