// CdiHandler.h — CDI DiscJuggler image handler for 7-zip

#ifndef ZIP7_INC_CDI_HANDLER_H
#define ZIP7_INC_CDI_HANDLER_H

#include "../../../Common/MyCom.h"
#include "../../../Common/MyString.h"
#include "../../../Common/MyVector.h"

#include "../IArchive.h"

namespace NArchive {
namespace NCdi {

struct CTrack {
    unsigned Session;
    unsigned TrackNum;
    unsigned Mode;         // 0=Audio, 1=Mode1, 2=Mode2
    unsigned SectorSize;   // 2048, 2336, 2352
    unsigned StartLBA;
    unsigned Pregap;
    unsigned DataSectors;
    unsigned TotalSectors;
    UInt64   FileOffset;
    UInt64   DataOffset;
    AString  Name;
};

struct CBootInfo {
    bool Valid;
    char HardwareId[17];
    char MakerId[17];
    char DeviceInfo[17];
    char Area[17];
    char Version[17];
    char Date[17];
    char BootFile[17];
};

Z7_CLASS_IMP_CHandler_IInArchive_1(
    IInArchiveGetStream
)
    CMyComPtr<IInStream> _stream;
    CObjectVector<CTrack> _tracks;
    CBootInfo             _boot;
    unsigned              _verMajor, _verMinor;

    HRESULT ParseCdi(IInStream *s);
    bool StripSector(const Byte *raw, unsigned ssize, unsigned mode, Byte *out);

public:
    CHandler();
};

}} // namespace

#endif
