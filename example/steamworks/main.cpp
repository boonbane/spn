#include <stdio.h>

#include "steam/steam_api_flat.h"

int main() {
  SteamErrMsg err = { 0 };
  if (SteamAPI_InitFlat(&err) != k_ESteamAPIInitResult_OK) {
    printf("steam is not available: %s\n", err);
    return 1;
  }

  const char* name = SteamAPI_ISteamFriends_GetPersonaName(SteamAPI_SteamFriends());
  unsigned long long id = SteamAPI_ISteamUser_GetSteamID(SteamAPI_SteamUser());
  unsigned int app = SteamAPI_ISteamUtils_GetAppID(SteamAPI_SteamUtils());
  printf("%s (%llu) is running app %u\n", name, id, app);

  SteamAPI_Shutdown();
  return 0;
}
