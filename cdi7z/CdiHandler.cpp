// CdiHandler.cpp — CDI DiscJuggler image handler for 7-zip
// Open-source replacement for Iso7z with IP.BIN boot sector support.

#include "StdAfx.h"

#include <cstdio>

#include "../../../Common/ComTry.h"
#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"

#include "../../../Windows/PropVariant.h"

#include "../../Common/LimitedStreams.h"
#include "../../Common/ProgressUtils.h"
#include "../../Common/StreamUtils.h"

#include "../../Compress/CopyCoder.h"

#include "../Common/ItemNameUtils.h"

#include "CdiHandler.h"

using namespace NWindows;

namespace NArchive {
namespace NCdi {

// ----------------------------------------------------------------
// Properties
// ----------------------------------------------------------------
enum {
    kpidCdiSession     = kpidUserDefined,
    kpidCdiTrackMode,
    kpidCdiSectorSize,
    kpidCdiTrackStartLBA,
    kpidCdiPregap
};

static const CStatProp kProps[] = {
    { "Path",       kpidPath,             VT_BSTR },
    { "Size",       kpidSize,             VT_UI8  },
    { "Packed Size",kpidPackSize,         VT_UI8  },
    { "Extension",  kpidExtension,        VT_BSTR },
    { "Session",    (PROPID)kpidCdiSession,       VT_UI4  },
    { "Track Mode", (PROPID)kpidCdiTrackMode,     VT_BSTR },
    { "Sector Size",(PROPID)kpidCdiSectorSize,    VT_UI4  },
    { "Start LBA",  (PROPID)kpidCdiTrackStartLBA, VT_UI4  },
    { "Pregap",     (PROPID)kpidCdiPregap,         VT_UI4  }
};

static const CStatProp kArcProps[] = {
    { "Comment", kpidComment, VT_BSTR }
};

IMP_IInArchive_Props_WITH_NAME
IMP_IInArchive_ArcProps_WITH_NAME

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------

static UInt32 rd32(const Byte *p) {
    return (UInt32)p[0] | ((UInt32)p[1] << 8) |
           ((UInt32)p[2] << 16) | ((UInt32)p[3] << 24);
}

static void Trim16(char *dst, const Byte *src) {
    memcpy(dst, src, 16); dst[16] = 0;
    for (int i = 15; i >= 0 && dst[i] == ' '; i--) dst[i] = 0;
}

static const Byte kMark[10] = { 0,0,1,0,0,0,0xFF,0xFF,0xFF,0xFF };

// Read |len| bytes from stream at |pos|.  Returns false on error.
static bool SeekRead(IInStream *s, UInt64 pos, Byte *buf, size_t len) {
    if (InStream_SeekSet(s, pos) != S_OK) return false;
    return ReadStream_FALSE(s, buf, len) == S_OK;
}

// ----------------------------------------------------------------
// CHandler constructor
// ----------------------------------------------------------------

CHandler::CHandler() {
    _verMajor = _verMinor = 0;
    memset(&_boot, 0, sizeof(_boot));
}

HRESULT CHandler::ParseCdi(IInStream *s) {
    // --- Read footer ---
    UInt64 fileSize;
    RINOK(s->Seek(0, STREAM_SEEK_END, &fileSize))
    if (fileSize < 8) return S_FALSE;

    Byte fbuf[8];
    if (!SeekRead(s, fileSize - 8, fbuf, 8)) return S_FALSE;

    UInt32 ver = rd32(fbuf);
    UInt32 hdrOff = rd32(fbuf + 4);
    if (hdrOff == 0) return S_FALSE;

    _verMajor = (ver == 0x80000004) ? 2 : (ver == 0x80000005) ? 3 : 3;
    _verMinor = (ver == 0x80000006) ? 5 : 0;

    UInt64 hdrPos = (ver == 0x80000006) ? (fileSize - hdrOff) : hdrOff;
    if (hdrPos >= fileSize) return S_FALSE;

    Byte sb[2];
    if (!SeekRead(s, hdrPos, sb, 2)) return S_FALSE;
    unsigned nSessions = sb[0];
    if (nSessions == 0 || nSessions > 99) return S_FALSE;

    UInt64 pos = hdrPos + 2;
    UInt64 dataOff = 0;

    for (unsigned si = 0; si < nSessions; si++) {
        if (si > 0) pos++;

        Byte tb[2];
        if (!SeekRead(s, pos, tb, 2)) return S_FALSE;
        unsigned nTracks = tb[0];
        pos += 2;

        for (unsigned ti = 0; ti < nTracks; ti++) {
            // Find track block via double start-mark
            UInt64 mp = pos;
            Byte tmpB[4];
            if (!SeekRead(s, mp, tmpB, 4)) return S_FALSE;
            mp += 4;
            if (rd32(tmpB) != 0) mp += 8;

            Byte mk[20];
            if (!SeekRead(s, mp, mk, 10)) return S_FALSE;
            mp += 10;
            if (memcmp(mk, kMark, 10)) return S_FALSE;

            if (!SeekRead(s, mp, mk, 10)) return S_FALSE;
            mp += 10;
            if (memcmp(mk, kMark, 10)) return S_FALSE;

            // After reading both marks, mp is at byte 12 of the track block.
            // Set th to byte 0 so field offsets (0x10, 0x30+F, etc.) work.
            UInt64 th = mp - 12;

            Byte F;
            if (!SeekRead(s, th + 0x10, &F, 1)) return S_FALSE;

            UInt16 ni;
            if (!SeekRead(s, th + 0x30 + F, (Byte*)&ni, 2)) return S_FALSE;
            unsigned I = ni * 4;
            UInt64 fb = th + 0x36 + F + I; // fixed field base

            Byte idxBuf[8];
            UInt32 idx0 = 0, idx1 = 0;
            if (ni >= 1) {
                size_t rlen = (ni >= 2) ? 8 : 4;
                if (!SeekRead(s, th + 0x30 + F + 2, idxBuf, rlen))
                    return S_FALSE;
                idx0 = rd32(idxBuf);
                if (ni >= 2) idx1 = rd32(idxBuf + 4);
            }

            Byte mode;
            if (!SeekRead(s, fb + 2, &mode, 1)) return S_FALSE;

            Byte lba[4];
            UInt32 startLBA = 0;
            if (SeekRead(s, fb + 0x12, lba, 4)) startLBA = rd32(lba);

            Byte rm[4];
            UInt32 readMode = 0;
            if (SeekRead(s, fb + 0x2A, rm, 4)) readMode = rd32(rm);
            unsigned ssize = (readMode == 0) ? 2048 : (readMode == 1) ? 2336 : 2352;

            CTrack t;
            t.Session      = si;
            t.TrackNum     = ti;
            t.Mode         = mode;
            t.SectorSize   = ssize;
            t.StartLBA     = startLBA;
            t.Pregap       = idx0;
            t.DataSectors  = idx1;
            t.TotalSectors = idx0 + idx1;
            t.FileOffset   = dataOff;
            t.DataOffset   = dataOff + (UInt64)idx0 * ssize;

            dataOff += (UInt64)t.TotalSectors * ssize;

            char nb[64];
            const char *mn = (mode == 0) ? "Audio" : (mode == 1) ? "Mode1" : "Mode2";
            snprintf(nb, sizeof(nb), "S%u_%s_%02u.%s",
                     si + 1, mn, ti + 1, mode ? "iso" : "wav");
            t.Name = nb;
            _tracks.Add(t);

            pos = th + 0xE4 + F + I;
        }
    }

    // Read IP.BIN from first data track
    for (unsigned i = 0; i < _tracks.Size(); i++) {
        const CTrack &t = _tracks[i];
        if (t.Mode != 0 && t.DataSectors > 0) {
            Byte sec[2048];
            UInt64 s0 = t.DataOffset;
            if (t.SectorSize == 2336) s0 += 8;
            if (s0 + 2048 <= fileSize && SeekRead(s, s0, sec, 2048)) {
                if (!memcmp(sec, "SEGA SEGAKATANA ", 16) ||
                    !memcmp(sec, "SEGA SEGASATAKA ", 16))
                {
                    _boot.Valid = true;
                    Trim16(_boot.HardwareId, sec + 0x00);
                    Trim16(_boot.MakerId,    sec + 0x10);
                    Trim16(_boot.DeviceInfo, sec + 0x20);
                    Trim16(_boot.Area,       sec + 0x30);
                    Trim16(_boot.Version,    sec + 0x40);
                    Trim16(_boot.Date,       sec + 0x50);
                    Trim16(_boot.BootFile,   sec + 0x60);
                }
            }
            break;
        }
    }
    return _tracks.Size() > 0 ? S_OK : S_FALSE;
}

bool CHandler::StripSector(const Byte *raw, unsigned ssize, unsigned mode, Byte *out) {
    if (ssize == 2048) { memcpy(out, raw, 2048); return true; }
    if (mode == 0)     { memcpy(out, raw, ssize);  return true; } // audio: passthrough
    if (mode == 2 && ssize == 2336) { memcpy(out, raw + 8, 2048); return true; }
    if (mode == 2 && ssize == 2352) { memcpy(out, raw + 24, 2048); return true; }
    if (mode == 1 && ssize == 2352) { memcpy(out, raw + 16, 2048); return true; }
    return false;
}

// ----------------------------------------------------------------
// IInArchive
// ----------------------------------------------------------------

Z7_COM7F_IMF(CHandler::Open(IInStream *stream,
    const UInt64 * /*maxCheckStartPosition*/,
    IArchiveOpenCallback * /*openCallback*/))
{
    COM_TRY_BEGIN
    Close();
    RINOK(ParseCdi(stream))
    _stream = stream;
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::Close())
{
    _tracks.Clear();
    _stream.Release();
    memset(&_boot, 0, sizeof(_boot));
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *numItems))
{
    *numItems = _tracks.Size();
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value))
{
    COM_TRY_BEGIN
    NCOM::CPropVariant prop;
    if (propID == kpidComment && _boot.Valid) {
        AString s;
        s += "IP.BIN: ";
        s += _boot.HardwareId;
        s += " | Maker: ";
        s += _boot.MakerId;
        s += " | Area: ";
        s += _boot.Area;
        s += " | Boot: ";
        s += _boot.BootFile;
        s += " | Version: ";
        s += _boot.Version;
        s += " | Date: ";
        s += _boot.Date;
        prop = s;
    }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value))
{
    COM_TRY_BEGIN
    NCOM::CPropVariant prop;

    if (index >= _tracks.Size()) return E_INVALIDARG;
    const CTrack &t = _tracks[index];

    switch (propID) {
    case kpidPath:
    {
        UString s = GetUnicodeString(t.Name);
        prop = s;
        break;
    }
    case kpidSize:
        prop = (UInt64)t.DataSectors * (t.Mode == 0 ? 2352 : 2048);
        break;
    case kpidPackSize:
        prop = (UInt64)t.TotalSectors * t.SectorSize;
        break;
    case kpidExtension:
        prop = t.Mode ? L"iso" : L"wav";
        break;
    case kpidIsDir:
        prop = false;
        break;
    case kpidCdiSession:
        prop = (UInt32)(t.Session + 1);
        break;
    case kpidCdiTrackMode: {
        const wchar_t *ms = (t.Mode == 0) ? L"Audio" : (t.Mode == 1) ? L"Mode1" : L"Mode2";
        prop = ms;
        break;
    }
    case kpidCdiSectorSize:
        prop = (UInt32)t.SectorSize;
        break;
    case kpidCdiTrackStartLBA:
        prop = (UInt32)t.StartLBA;
        break;
    case kpidCdiPregap:
        prop = (UInt32)t.Pregap;
        break;
    default:
        // Return empty for unsupported properties
        break;
    }

    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::Extract(const UInt32 *indices, UInt32 numItems,
    Int32 testMode, IArchiveExtractCallback *extractCallback))
{
    COM_TRY_BEGIN
    const bool allFilesMode = (numItems == (UInt32)(Int32)-1);
    if (allFilesMode) numItems = _tracks.Size();
    if (numItems == 0) return S_OK;

    UInt64 totalSize = 0;
    for (UInt32 i = 0; i < numItems; i++) {
        UInt32 idx = allFilesMode ? i : indices[i];
        if (idx < _tracks.Size()) {
            const CTrack &tt = _tracks[idx];
            totalSize += (UInt64)tt.DataSectors * (tt.Mode == 0 ? 2352 : 2048);
        }
    }
    RINOK(extractCallback->SetTotal(totalSize))

    UInt64 cur = 0;
    Byte raw[2352], out[2048];

    for (UInt32 i = 0; i < numItems; i++) {
        UInt32 idx = allFilesMode ? i : indices[i];
        if (idx >= _tracks.Size()) continue;

        const CTrack &t = _tracks[idx];
        unsigned outSectorSize = t.Mode == 0 ? 2352 : 2048;
        UInt64 sz = (UInt64)t.DataSectors * outSectorSize;

        RINOK(extractCallback->SetCompleted(&cur))

        CMyComPtr<ISequentialOutStream> os;
        const Int32 askMode = testMode ?
            NExtract::NAskMode::kTest : NExtract::NAskMode::kExtract;
        RINOK(extractCallback->GetStream(idx, &os, askMode))

        if (!testMode && !os) { cur += sz; continue; }

        RINOK(extractCallback->PrepareOperation(askMode))

        Int32 opRes = NExtract::NOperationResult::kOK;
        for (UInt32 sec = 0; sec < t.DataSectors; sec++) {
            UInt64 so = t.DataOffset + (UInt64)sec * t.SectorSize;
            RINOK(InStream_SeekSet(_stream, so))
            size_t processed = t.SectorSize;
            RINOK(ReadStream(_stream, raw, &processed))
            if (processed != t.SectorSize) {
                opRes = NExtract::NOperationResult::kDataError;
                break;
            }
            if (!StripSector(raw, t.SectorSize, t.Mode, out)) {
                opRes = NExtract::NOperationResult::kDataError;
                break;
            }
            if (testMode) continue;
            UInt32 done;
            RINOK(os->Write(out, outSectorSize, &done))
            if (done != outSectorSize) {
                opRes = NExtract::NOperationResult::kDataError;
                break;
            }
        }
        cur += sz;
        RINOK(extractCallback->SetOperationResult(opRes))
    }
    return S_OK;
    COM_TRY_END
}

// ----------------------------------------------------------------
// CSectorStream — strips CDI sector headers for nested ISO browsing
// Given a Mode2/2336 track, exposes it as clean 2048-byte ISO sectors.
// ----------------------------------------------------------------
class CSectorStream:
    public ISequentialInStream,
    public CMyUnknownImp
{
    CMyComPtr<IInStream> _stream;
    UInt64    _dataStart;
    UInt64    _virtPos;
    UInt64    _virtSize;
    unsigned  _rawSectorSize;
    unsigned  _stripOffset;

public:
    CSectorStream(IInStream *stream, const CTrack &t):
        _stream(stream),
        _dataStart(t.DataOffset),
        _virtPos(0),
        _virtSize((UInt64)t.DataSectors * 2048),
        _rawSectorSize(t.SectorSize),
        _stripOffset(t.SectorSize == 2336 ? 8 : 24) {}

    Z7_IFACES_IMP_UNK_1(ISequentialInStream)
    virtual ~CSectorStream() {}
};

Z7_COM7F_IMF(CSectorStream::Read(void *data, UInt32 size, UInt32 *processed))
{
    if (_virtPos >= _virtSize) {
        *processed = 0;
        return S_OK;
    }
    UInt64 remain = _virtSize - _virtPos;
    if (size > remain) size = (UInt32)remain;

    UInt32 written = 0;
    Byte rawBuf[2352];
    while (written < size) {
        UInt64 sectorIndex = (_virtPos + written) / 2048;
        unsigned offsetInSector = (unsigned)((_virtPos + written) % 2048);
        unsigned toCopy = 2048 - offsetInSector;
        if (toCopy > size - written) toCopy = size - written;

        UInt64 rawPos = _dataStart + sectorIndex * _rawSectorSize;
        RINOK(InStream_SeekSet(_stream, rawPos))
        size_t readSz = _rawSectorSize;
        RINOK(ReadStream(_stream, rawBuf, &readSz))
        if (readSz != _rawSectorSize) break;

        memcpy((Byte*)data + written, rawBuf + _stripOffset + offsetInSector, toCopy);
        written += toCopy;
    }
    _virtPos += written;
    *processed = written;
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetStream(UInt32 index, ISequentialInStream **stream))
{
    COM_TRY_BEGIN
    *stream = NULL;
    if (index >= _tracks.Size()) return E_INVALIDARG;
    const CTrack &t = _tracks[index];
    if (t.Mode == 0) return S_FALSE;
    if (t.DataSectors == 0) return S_FALSE;

    *stream = new CSectorStream(_stream, t);
    return S_OK;
    COM_TRY_END
}

}} // namespace
