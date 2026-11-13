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
  mkdir("/work/gen/G", 0755);
  for (struct dirent* entry = readdir(dir); entry; entry = readdir(dir)) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    FILE* file = fopen(spn_fmt("/source/H/{}", entry->d_name), "r");
    if (!file) {
      closedir(dir);
      return 1;
    }
    c8 content [64] = { 0 };
    fread(content, 1, sizeof(content) - 1, file);
    fclose(file);
    file = fopen(spn_fmt("/work/gen/G/{}", entry->d_name), "w");
    if (!file) {
      closedir(dir);
      return 1;
    }
    fputs(content, file);
    fclose(file);
  }
  closedir(dir);
  return 0;
}
