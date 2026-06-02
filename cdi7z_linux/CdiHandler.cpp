// CdiHandler.cpp — CDI DiscJuggler image handler for 7-zip
// Open-source replacement for Iso7z with IP.BIN support and
// single-step file extraction via built-in ISO9660 walker.

#include "StdAfx.h"

#include <cstdio>

#include "../../../Common/ComTry.h"
#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"

#include "../../../Windows/PropVariant.h"
#include "../../../Windows/TimeUtils.h"

#include "../../Common/LimitedStreams.h"
#include "../../Common/ProgressUtils.h"
#include "../../Common/StreamUtils.h"

#include "../../Compress/CopyCoder.h"

#include "CdiHandler.h"

using namespace NWindows;
using namespace NTime;

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
    { "Modified",   kpidMTime,            VT_FILETIME },
    { "Attributes", kpidAttrib,           VT_UI4  },
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

static bool SeekRead(IInStream *s, UInt64 pos, Byte *buf, size_t len) {
    if (InStream_SeekSet(s, pos) != S_OK) return false;
    return ReadStream_FALSE(s, buf, len) == S_OK;
}

// Convert ISO9660 date (year-1900, month, day, hour, minute, second)
// to FILETIME. Simple day-count algorithm.
static void IsoDateTimeToFileTime(const Byte *d, FILETIME &ft) {
    memset(&ft, 0, sizeof(ft));
    if (d[0] == 0) return;

    int y = d[0] + 1900;
    int m = d[1];
    int day = d[2];
    int h = d[3];
    int min = d[4];
    int sec = d[5];

    // Days from 1970-01-01 to given date
    if (m <= 2) { m += 12; y--; }
    int days = 365*y + y/4 - y/100 + y/400 + (153*(m+1))/5 + day - 719528;
    Int64 unixTime = (Int64)days * 86400 + (Int64)h * 3600 + (Int64)min * 60 + sec;
    UnixTime64_To_FileTime(unixTime, ft);
}

// ----------------------------------------------------------------
// CHandler constructor
// ----------------------------------------------------------------

CHandler::CHandler() {
    _verMajor = _verMinor = 0;
    memset(&_boot, 0, sizeof(_boot));
}

// ================================================================
// CDI Parser
// ================================================================

HRESULT CHandler::ParseCdi(IInStream *s) {
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

            UInt64 th = mp - 12;

            Byte F;
            if (!SeekRead(s, th + 0x10, &F, 1)) return S_FALSE;

            UInt16 ni;
            if (!SeekRead(s, th + 0x30 + F, (Byte*)&ni, 2)) return S_FALSE;
            unsigned I = ni * 4;
            UInt64 fb = th + 0x36 + F + I;

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

// ================================================================
// ISO9660 filesystem walker
// ================================================================

bool CHandler::ReadIsoSector(Byte *buf2048, unsigned sectorIndex) {
    // Use first data track for ISO9660
    const CTrack *dTrack = nullptr;
    for (unsigned i = 0; i < _tracks.Size(); i++) {
        if (_tracks[i].Mode != 0 && _tracks[i].DataSectors > 0) {
            dTrack = &_tracks[i];
            break;
        }
    }
    if (!dTrack || sectorIndex >= dTrack->DataSectors)
        return false;

    Byte raw[2352];
    UInt64 off = dTrack->DataOffset + (UInt64)sectorIndex * dTrack->SectorSize;
    if (!SeekRead(_stream, off, raw, dTrack->SectorSize))
        return false;

    return StripSector(raw, dTrack->SectorSize, dTrack->Mode, buf2048);
}

HRESULT CHandler::ParseIso9660() {
    // Find first data track
    const CTrack *dTrack = nullptr;
    for (unsigned i = 0; i < _tracks.Size(); i++) {
        if (_tracks[i].Mode != 0 && _tracks[i].DataSectors > 0) {
            dTrack = &_tracks[i];
            break;
        }
    }
    if (!dTrack) return S_FALSE;  // audio-only disc, keep track view

    // Read PVD at sector 16 (or 0 as fallback)
    Byte pvd[2048];
    bool foundPvd = false;
    UInt32 rootExtent = 0, rootSize = 0;

    int pvdSectors[2] = {16, 0};
    for (int pi = 0; pi < 2; pi++) {
        int pvdSec = pvdSectors[pi];
        if (!ReadIsoSector(pvd, (unsigned)pvdSec)) continue;
        if (pvd[0] != 0x01) continue;
        if (memcmp(pvd + 1, "CD001", 5) != 0) continue;
        if (pvd[6] != 0x01) continue;

        const Byte *root = pvd + 156;
        if (root[0] < 34) continue;

        rootExtent = rd32(root + 2);
        rootSize   = rd32(root + 10);
        foundPvd = true;
        break;
    }
    if (!foundPvd) return S_FALSE;

    // Recursively walk directory tree.
    // We use a manual stack to avoid loading all sectors into memory.
    struct DirJob {
        UInt32 Extent;
        UInt32 Size;
        AString Prefix;
    };

    CObjectVector<DirJob> queue;
    {
        DirJob job;
        job.Extent = rootExtent;
        job.Size   = rootSize;
        job.Prefix = "";
        queue.Add(job);
    }

    for (unsigned qi = 0; qi < queue.Size(); qi++) {
        const DirJob &job = queue[qi];

        // Read directory sectors and collect entries
        Int64 localSec = (Int64)job.Extent - (Int64)dTrack->StartLBA;
        if (localSec < 0 || (UInt64)localSec >= dTrack->DataSectors) continue;

        UInt32 so = (UInt32)localSec;
        UInt32 bytesRead = 0;
        Byte dirSector[2048];

        while (bytesRead < job.Size && so < dTrack->DataSectors) {
            if (!ReadIsoSector(dirSector, so)) break;
            UInt32 off = 0;

            while (off < 2048 && bytesRead < job.Size) {
                unsigned recLen = dirSector[off];
                if (recLen == 0) {
                    bytesRead += 2048 - off;
                    break;
                }
                if (recLen < 34 || off + recLen > 2048) break;

                const Byte *rec = dirSector + off;
                Byte flags    = rec[25];
                UInt32 fExtent  = rd32(rec + 2);
                UInt32 fSize    = rd32(rec + 10);
                Byte idLen    = rec[32];
                bool   isDir    = (flags & 0x02) != 0;

                // Skip . and ..
                if (!(idLen == 1 && (rec[33] == 0x00 || rec[33] == 0x01))) {
                    // Extract filename (ISO9660 or Rock Ridge)
                    AString fname;
                    {
                        // Try Rock Ridge NM for long filename
                        unsigned suOff = 33 + idLen;
                        unsigned suEnd = recLen;
                        bool gotRR = false;
                        while (suOff + 4 <= suEnd) {
                            if (rec[suOff] == 'N' && rec[suOff + 1] == 'M') {
                                unsigned elen = rec[suOff + 2];
                                if (elen >= 5 && suOff + elen <= suEnd) {
                                    unsigned rrFlags = rec[suOff + 4];
                                    if (!(rrFlags & 0x06)) {
                                        fname.SetFrom((const char*)(rec + suOff + 5), elen - 5);
                                        gotRR = true;
                                    }
                                }
                            }
                            if (rec[suOff + 2] < 4) break;
                            suOff += rec[suOff + 2];
                        }
                        if (!gotRR) {
                            fname.SetFrom((const char*)(rec + 33), idLen);
                            int sc = fname.Find(';');
                            if (sc >= 0) fname.DeleteFrom(sc);
                            // trim trailing spaces
                            while (!fname.IsEmpty() && fname.Back() == ' ')
                                fname.DeleteBack();
                        }
                    }

                    if (!fname.IsEmpty()) {
                        AString fullPath = job.Prefix;
                        if (!fullPath.IsEmpty()) fullPath += '/';
                        fullPath += fname;

                        if (isDir) {
                            fullPath += '/';
                            DirJob child;
                            child.Extent = fExtent;
                            child.Size   = fSize;
                            child.Prefix = fullPath;
                            queue.Add(child);
                        }

                        // Add file/dir to item list
                        CFileItem fi;
                        fi.Path      = fullPath;
                        fi.ExtentLBA = fExtent;
                        fi.DataSize  = isDir ? 0 : fSize;
                        fi.IsDir     = isDir;
                        IsoDateTimeToFileTime(rec + 18, fi.MTime);
                        _files.Add(fi);
                    }
                }

                off += recLen;
                bytesRead += recLen;
            }
            so++;
        }
    }

    return _files.Size() > 0 ? S_OK : S_FALSE;
}

// ================================================================
// Sector stripping
// ================================================================

bool CHandler::StripSector(const Byte *raw, unsigned ssize, unsigned mode, Byte *out) {
    if (ssize == 2048) { memcpy(out, raw, 2048); return true; }
    if (mode == 0)     { memcpy(out, raw, ssize);  return true; }
    if (mode == 2 && ssize == 2336) { memcpy(out, raw + 8, 2048); return true; }
    if (mode == 2 && ssize == 2352) { memcpy(out, raw + 24, 2048); return true; }
    if (mode == 1 && ssize == 2352) { memcpy(out, raw + 16, 2048); return true; }
    return false;
}

// ================================================================
// IInArchive
// ================================================================

Z7_COM7F_IMF(CHandler::Open(IInStream *stream,
    const UInt64 * /*maxCheckStartPosition*/,
    IArchiveOpenCallback * /*openCallback*/))
{
    COM_TRY_BEGIN
    Close();
    RINOK(ParseCdi(stream))
    _stream = stream;
    ParseIso9660();  // best-effort, may fail for audio-only
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::Close())
{
    _tracks.Clear();
    _files.Clear();
    _stream.Release();
    memset(&_boot, 0, sizeof(_boot));
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *numItems))
{
    // If ISO9660 files were found, show files. Otherwise show tracks.
    if (_files.Size() > 0)
        *numItems = _files.Size();
    else
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

    if (_files.Size() > 0) {
        // File-level view
        if (index >= _files.Size()) return E_INVALIDARG;
        const CFileItem &f = _files[index];

        switch (propID) {
        case kpidPath:
        {
            UString s = GetUnicodeString(f.Path);
            prop = s;
            break;
        }
        case kpidSize:
            prop = (UInt64)f.DataSize;
            break;
        case kpidPackSize:
            prop = (UInt64)f.DataSize;
            break;
        case kpidIsDir:
            prop = f.IsDir;
            break;
        case kpidMTime:
            prop = f.MTime;
            break;
        case kpidAttrib:
            prop = (UInt32)(f.IsDir ? 0x10 : 0x20);
            break;
        default:
            break;
        }
    } else {
        // Track-level fallback
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
            break;
        }
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
    if (allFilesMode) {
        numItems = (_files.Size() > 0) ? _files.Size() : _tracks.Size();
    }
    if (numItems == 0) return S_OK;

    // Find first data track for ISO9660 reading
    const CTrack *dTrack = nullptr;
    for (unsigned i = 0; i < _tracks.Size(); i++) {
        if (_tracks[i].Mode != 0 && _tracks[i].DataSectors > 0) {
            dTrack = &_tracks[i];
            break;
        }
    }

    if (_files.Size() > 0 && dTrack) {
        // --- File-level extraction ---
        UInt64 totalSize = 0;
        for (UInt32 i = 0; i < numItems; i++) {
            UInt32 idx = allFilesMode ? i : indices[i];
            if (idx < _files.Size() && !_files[idx].IsDir)
                totalSize += _files[idx].DataSize;
        }
        RINOK(extractCallback->SetTotal(totalSize))

        UInt64 cur = 0;
        Byte raw[2352], out[2048];

        for (UInt32 i = 0; i < numItems; i++) {
            UInt32 idx = allFilesMode ? i : indices[i];
            if (idx >= _files.Size()) continue;
            const CFileItem &f = _files[idx];

            RINOK(extractCallback->SetCompleted(&cur))
            CMyComPtr<ISequentialOutStream> os;
            const Int32 askMode = testMode ?
                NExtract::NAskMode::kTest : NExtract::NAskMode::kExtract;
            RINOK(extractCallback->GetStream(idx, &os, askMode))

            if (f.IsDir || (!testMode && !os)) {
                cur += f.DataSize;
                if (f.IsDir) {
                    RINOK(extractCallback->PrepareOperation(askMode))
                    RINOK(extractCallback->SetOperationResult(
                        NExtract::NOperationResult::kOK))
                }
                continue;
            }

            RINOK(extractCallback->PrepareOperation(askMode))
            Int32 opRes = NExtract::NOperationResult::kOK;

            // Compute position within data track
            Int64 localSec = (Int64)f.ExtentLBA - (Int64)dTrack->StartLBA;
            if (localSec < 0 || (UInt64)localSec >= dTrack->DataSectors) {
                opRes = NExtract::NOperationResult::kDataError;
            } else {
                UInt64 bytesLeft = f.DataSize;
                UInt32 secIdx = (UInt32)localSec;

                while (bytesLeft > 0 && secIdx < dTrack->DataSectors) {
                    if (testMode) {
                        UInt32 chunk = (bytesLeft < 2048) ? (UInt32)bytesLeft : 2048;
                        bytesLeft -= chunk;
                        secIdx++;
                        continue;
                    }

                    UInt64 secOff = dTrack->DataOffset + (UInt64)secIdx * dTrack->SectorSize;
                    size_t processed = dTrack->SectorSize;
                    RINOK(InStream_SeekSet(_stream, secOff))
                    RINOK(ReadStream(_stream, raw, &processed))
                    if (processed != dTrack->SectorSize) {
                        opRes = NExtract::NOperationResult::kDataError;
                        break;
                    }
                    if (!StripSector(raw, dTrack->SectorSize, dTrack->Mode, out)) {
                        opRes = NExtract::NOperationResult::kDataError;
                        break;
                    }

                    UInt32 chunk = (bytesLeft < 2048) ? (UInt32)bytesLeft : 2048;
                    UInt32 done;
                    RINOK(os->Write(out, chunk, &done))
                    if (done != chunk) {
                        opRes = NExtract::NOperationResult::kDataError;
                        break;
                    }
                    bytesLeft -= chunk;
                    secIdx++;
                }
                if (bytesLeft > 0 && opRes == NExtract::NOperationResult::kOK)
                    opRes = NExtract::NOperationResult::kDataError;
            }
            cur += f.DataSize;
            RINOK(extractCallback->SetOperationResult(opRes))
        }
    } else {
        // --- Track-level fallback ---
        UInt64 totalSize = 0;
        for (UInt32 i = 0; i < numItems; i++) {
            UInt32 idx = allFilesMode ? i : indices[i];
            if (idx < _tracks.Size())
                totalSize += (UInt64)_tracks[idx].DataSectors *
                             (_tracks[idx].Mode == 0 ? 2352 : 2048);
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
                size_t processed = t.SectorSize;
                RINOK(InStream_SeekSet(_stream, so))
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
    }
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetStream(UInt32 /*index*/, ISequentialInStream **stream))
{
    COM_TRY_BEGIN
    *stream = NULL;
    return S_FALSE;
    COM_TRY_END
}

}} // namespace
