#include "ppc_recomp_shared.h"
// Entirely synthetic: validates cross-object linkage of the native bridge.
PPCFuncMapping PPCFuncMappings[] = {
    {0x8277F670, _xstart},
    {0x82500000, __imp__XamInputGetState},
    {0x82500004, __imp__KeEnterCriticalRegion},
    {0, nullptr}
};
