// CdiRegister.cpp — register CDI format with 7-zip

#include "StdAfx.h"

// REGISTER_ARC macros create a static CreateArc() that is used via
// function pointer (stored in g_ArcInfo).  GCC's -Wunused-function
// cannot see through the pointer, so suppress the false positive.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "../../Common/RegisterArc.h"

#include "CdiHandler.h"

namespace NArchive {
namespace NCdi {

REGISTER_ARC_I_CLS_NO_SIG(
    CHandler(),
    "CDI", "cdi", nullptr, 0x8C,
    0,
    NArcInfoFlags::kStartOpen,
    NULL)

}} // namespace
