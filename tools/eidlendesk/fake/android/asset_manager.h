/* the EID length desk: the asset manager, answered from the APK's assets folder (desk.c) */
#ifndef DESK_ASSET_MANAGER_H
#define DESK_ASSET_MANAGER_H
typedef struct AAssetManager AAssetManager;
typedef struct AAsset AAsset;
#define AASSET_MODE_BUFFER 3
AAsset *AAssetManager_open(AAssetManager *mgr, const char *name, int mode);
const void *AAsset_getBuffer(AAsset *a);
long long AAsset_getLength64(AAsset *a);
void AAsset_close(AAsset *a);
#endif
