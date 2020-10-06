/* akvirtualcamera, virtual camera for Mac and Windows.
 * Copyright (C) 2020  Gonzalo Exequiel Pedone
 *
 * akvirtualcamera is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * akvirtualcamera is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with akvirtualcamera. If not, see <http://www.gnu.org/licenses/>.
 *
 * Web-Site: http://webcamoid.github.io/
 */

#include <algorithm>
#include <cwchar>
#include <map>
#include <sstream>
#include <dshow.h>
#include <dvdmedia.h>
#include <comdef.h>
#include <shlobj.h>

#include "utils.h"
#include "VCamUtils/src/utils.h"
#include "VCamUtils/src/image/videoformat.h"

#define TIME_BASE 1.0e7

namespace AkVCam
{
    class VideoFormatSpecsPrivate
    {
        public:
            FourCC pixelFormat;
            DWORD compression;
            GUID guid;
            const DWORD *masks;

            inline static const std::vector<VideoFormatSpecsPrivate> &formats()
            {
                static const DWORD bits555[] = {0x007c00, 0x0003e0, 0x00001f};
                static const DWORD bits565[] = {0x00f800, 0x0007e0, 0x00001f};

                static const std::vector<VideoFormatSpecsPrivate> formats {
                    {PixelFormatRGB32, BI_RGB                        , MEDIASUBTYPE_RGB32 , nullptr},
                    {PixelFormatRGB24, BI_RGB                        , MEDIASUBTYPE_RGB24 , nullptr},
                    {PixelFormatRGB16, BI_BITFIELDS                  , MEDIASUBTYPE_RGB565, bits565},
                    {PixelFormatRGB15, BI_BITFIELDS                  , MEDIASUBTYPE_RGB555, bits555},
                    {PixelFormatUYVY , MAKEFOURCC('U', 'Y', 'V', 'Y'), MEDIASUBTYPE_UYVY  , nullptr},
                    {PixelFormatYUY2 , MAKEFOURCC('Y', 'U', 'Y', '2'), MEDIASUBTYPE_YUY2  , nullptr},
                    {PixelFormatNV12 , MAKEFOURCC('N', 'V', '1', '2'), MEDIASUBTYPE_NV12  , nullptr}
                };

                return formats;
            }

            static inline const VideoFormatSpecsPrivate *byGuid(const GUID &guid)
            {
                for (auto &format: formats())
                    if (IsEqualGUID(format.guid, guid))
                        return &format;

                return nullptr;
            }

            static inline const VideoFormatSpecsPrivate *byPixelFormat(FourCC pixelFormat)
            {
                for (auto &format: formats())
                    if (format.pixelFormat == pixelFormat)
                        return &format;

                return nullptr;
            }
    };
}

bool operator <(const CLSID &a, const CLSID &b)
{
    return AkVCam::stringFromIid(a) < AkVCam::stringFromIid(b);
}

BOOL AkVCam::isWow64()
{
    BOOL isWow64 = FALSE;

    if (!IsWow64Process(GetCurrentProcess(), &isWow64))
        return false;

    return isWow64;
}

std::string AkVCam::tempPath()
{
    CHAR tempPath[MAX_PATH];
    memset(tempPath, 0, MAX_PATH * sizeof(CHAR));
    GetTempPathA(MAX_PATH, tempPath);

    return std::string(tempPath);
}

std::wstring AkVCam::programFilesPath()
{
    bool ok = false;
    TCHAR programFiles[MAX_PATH];
    DWORD programFilesSize = MAX_PATH * sizeof(TCHAR);
    memset(programFiles, 0, programFilesSize);
    HKEY hkey = nullptr;
    auto result = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                               L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
                               0,
                               KEY_READ | KEY_WOW64_64KEY,
                               &hkey);

    if (result == ERROR_SUCCESS) {
        result = RegGetValue(hkey,
                             nullptr,
                             L"ProgramFilesDir",
                             RRF_RT_REG_SZ,
                             nullptr,
                             &programFiles,
                             &programFilesSize);
        if (isWow64())
            ok = true;

        RegCloseKey(hkey);
    }

    if (!ok)
        SHGetSpecialFolderPath(nullptr,
                               programFiles,
                               CSIDL_PROGRAM_FILES,
                               FALSE);

    return std::wstring(programFiles);
}

std::wstring AkVCam::moduleFileNameW(HINSTANCE hinstDLL)
{
    WCHAR fileName[MAX_PATH];
    memset(fileName, 0, MAX_PATH * sizeof(WCHAR));
    GetModuleFileName(hinstDLL, fileName, MAX_PATH);

    return std::wstring(fileName);
}

std::string AkVCam::moduleFileName(HINSTANCE hinstDLL)
{
    auto fileName = moduleFileNameW(hinstDLL);

    return std::string(fileName.begin(), fileName.end());
}

std::wstring AkVCam::errorToStringW(DWORD errorCode)
{
    WCHAR *errorStr = nullptr;
    auto size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER
                              | FORMAT_MESSAGE_FROM_SYSTEM
                              | FORMAT_MESSAGE_IGNORE_INSERTS,
                              nullptr,
                              errorCode,
                              MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                              reinterpret_cast<LPWSTR>(&errorStr),
                              0,
                              nullptr);
    std::wstring error(errorStr, size);
    LocalFree(errorStr);

    return error;
}

std::string AkVCam::errorToString(DWORD errorCode)
{
    auto errorStr = errorToStringW(errorCode);

    return std::string(errorStr.begin(), errorStr.end());
}

// Converts a human redable string to a CLSID using MD5 hash.
CLSID AkVCam::createClsidFromStr(const std::string &str)
{
    return createClsidFromStr(std::wstring(str.begin(), str.end()));
}

CLSID AkVCam::createClsidFromStr(const std::wstring &str)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    CLSID clsid;
    DWORD clsidLen = sizeof(CLSID);
    memset(&clsid, 0, sizeof(CLSID));

    if (!CryptAcquireContext(&provider,
                             nullptr,
                             nullptr,
                             PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT))
        goto clsidFromStr_failed;

    if (!CryptCreateHash(provider, CALG_MD5, 0, 0, &hash))
        goto clsidFromStr_failed;

    if (!CryptHashData(hash,
                       reinterpret_cast<const BYTE *>(str.c_str()),
                       DWORD(str.size() * sizeof(wchar_t)),
                       0))
        goto clsidFromStr_failed;

    CryptGetHashParam(hash,
                      HP_HASHVAL,
                      reinterpret_cast<BYTE *>(&clsid),
                      &clsidLen,
                      0);

clsidFromStr_failed:
    if (hash)
        CryptDestroyHash(hash);

    if (provider)
        CryptReleaseContext(provider, 0);

    return clsid;
}

std::wstring AkVCam::createClsidWStrFromStr(const std::string &str)
{
    return createClsidWStrFromStr(std::wstring(str.begin(), str.end()));
}

std::wstring AkVCam::createClsidWStrFromStr(const std::wstring &str)
{
    auto clsid = createClsidFromStr(str);
    OLECHAR *clsidWStr = nullptr;

    if (StringFromCLSID(clsid, &clsidWStr) != S_OK)
        return std::wstring();

    std::wstring wstr(clsidWStr);
    CoTaskMemFree(clsidWStr);

    return wstr;
}

std::string AkVCam::stringFromIid(const IID &iid)
{
    auto wstr = wstringFromIid(iid);

    return std::string(wstr.begin(), wstr.end());
}

std::wstring AkVCam::wstringFromIid(const IID &iid)
{
    WCHAR *strIID = nullptr;
    StringFromIID(iid, &strIID);
    std::wstring wstr(strIID);
    CoTaskMemFree(strIID);

    return wstr;
}

std::string AkVCam::stringFromResult(HRESULT result)
{
    auto msg = std::wstring(_com_error(result).ErrorMessage());

    return std::string(msg.begin(), msg.end());
}

std::string AkVCam::stringFromClsid(const CLSID &clsid)
{
    static const std::map<CLSID, std::string> clsidToString {
        {IID_IAgileObject         , "IAgileObject"         },
        {IID_IAMAnalogVideoDecoder, "IAMAnalogVideoDecoder"},
        {IID_IAMAudioInputMixer   , "IAMAudioInputMixer"   },
        {IID_IAMAudioRendererStats, "IAMAudioRendererStats"},
        {IID_IAMBufferNegotiation , "IAMBufferNegotiation" },
        {IID_IAMCameraControl     , "IAMCameraControl"     },
        {IID_IAMClockAdjust       , "IAMClockAdjust"       },
        {IID_IAMCrossbar          , "IAMCrossbar"          },
        {IID_IAMDeviceRemoval     , "IAMDeviceRemoval"     },
        {IID_IAMExtDevice         , "IAMExtDevice"         },
        {IID_IAMFilterMiscFlags   , "IAMFilterMiscFlags"   },
        {IID_IAMOpenProgress      , "IAMOpenProgress"      },
        {IID_IAMPushSource        , "IAMPushSource"        },
        {IID_IAMStreamConfig      , "IAMStreamConfig"      },
        {IID_IAMTVTuner           , "IAMTVTuner"           },
        {IID_IAMVfwCaptureDialogs , "IAMVfwCaptureDialogs" },
        {IID_IAMVfwCompressDialogs, "IAMVfwCompressDialogs"},
        {IID_IAMVideoCompression  , "IAMVideoCompression"  },
        {IID_IAMVideoControl      , "IAMVideoControl"      },
        {IID_IAMVideoProcAmp      , "IAMVideoProcAmp"      },
        {IID_IBaseFilter          , "IBaseFilter"          },
        {IID_IBasicAudio          , "IBasicAudio"          },
        {IID_IBasicVideo          , "IBasicVideo"          },
        {IID_IClassFactory        , "IClassFactory"        },
        {IID_IEnumMediaTypes      , "IEnumMediaTypes"      },
        {IID_IEnumPins            , "IEnumPins"            },
        {IID_IFileSinkFilter      , "IFileSinkFilter"      },
        {IID_IFileSinkFilter2     , "IFileSinkFilter2"     },
        {IID_IFileSourceFilter    , "IFileSourceFilter"    },
        {IID_IKsPropertySet       , "IKsPropertySet"       },
        {IID_IMarshal             , "IMarshal"             },
        {IID_IMediaControl        , "IMediaControl"        },
        {IID_IMediaFilter         , "IMediaFilter"         },
        {IID_IMediaPosition       , "IMediaPosition"       },
        {IID_IMediaSample         , "IMediaSample"         },
        {IID_IMediaSample2        , "IMediaSample2"        },
        {IID_IMediaSeeking        , "IMediaSeeking"        },
        {IID_IMediaEventSink      , "IMediaEventSink"      },
        {IID_IMemAllocator        , "IMemAllocator"        },
        {IID_INoMarshal           , "INoMarshal"           },
        {IID_IPersist             , "IPersist"             },
        {IID_IPersistPropertyBag  , "IPersistPropertyBag"  },
        {IID_IPin                 , "IPin"                 },
        {IID_IProvideClassInfo    , "IProvideClassInfo"    },
        {IID_IQualityControl      , "IQualityControl"      },
        {IID_IReferenceClock      , "IReferenceClock"      },
        {IID_IRpcOptions          , "IRpcOptions"          },
        {IID_ISpecifyPropertyPages, "ISpecifyPropertyPages"},
        {IID_IVideoWindow         , "IVideoWindow"         },
        {IID_IUnknown             , "IUnknown"             },
    };

    for (auto &id: clsidToString)
        if (IsEqualCLSID(id.first, clsid))
            return id.second;

    return stringFromIid(clsid);
}

wchar_t *AkVCam::wcharStrFromWStr(const std::wstring &wstr)
{
    if (wstr.size() < 1)
        return nullptr;

    auto wcstrSize = wstr.size() * sizeof(wchar_t);
    auto wcstr = reinterpret_cast<wchar_t *>(CoTaskMemAlloc(wcstrSize + 1));
    wcstr[wstr.size()] = 0;
    memcpy(wcstr, wstr.data(), wcstrSize);

    return wcstr;
}

AkVCam::FourCC AkVCam::formatFromGuid(const GUID &guid)
{
    auto formatSpec = VideoFormatSpecsPrivate::byGuid(guid);

    if (!formatSpec)
        return 0;

    return formatSpec->pixelFormat;
}

const GUID &AkVCam::guidFromFormat(FourCC format)
{
    auto formatSpec = VideoFormatSpecsPrivate::byPixelFormat(format);

    if (!formatSpec)
        return GUID_NULL;

    return formatSpec->guid;
}

DWORD AkVCam::compressionFromFormat(FourCC format)
{
    auto formatSpec = VideoFormatSpecsPrivate::byPixelFormat(format);

    if (!formatSpec)
        return 0;

    return formatSpec->compression;
}

bool AkVCam::isSubTypeSupported(const GUID &subType)
{
    for (auto &format: VideoFormatSpecsPrivate::formats())
        if (IsEqualGUID(format.guid, subType))
            return true;

    return false;
}

AM_MEDIA_TYPE *AkVCam::mediaTypeFromFormat(const AkVCam::VideoFormat &format)
{
    auto subtype = guidFromFormat(format.fourcc());

    if (IsEqualGUID(subtype, GUID_NULL))
        return nullptr;

    auto frameSize = format.size();

    if (!frameSize)
        return nullptr;

    auto videoInfo =
            reinterpret_cast<VIDEOINFO *>(CoTaskMemAlloc(sizeof(VIDEOINFO)));
    memset(videoInfo, 0, sizeof(VIDEOINFO));
    auto fps = format.minimumFrameRate();


    // Initialize info header.
    videoInfo->rcSource = {0, 0, 0, 0};
    videoInfo->rcTarget = videoInfo->rcSource;
    videoInfo->dwBitRate = DWORD(8
                                 * frameSize
                                 * size_t(fps.num())
                                 / size_t(fps.den()));
    videoInfo->AvgTimePerFrame = REFERENCE_TIME(TIME_BASE / fps.value());

    // Initialize bitmap header.
    videoInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    videoInfo->bmiHeader.biWidth = format.width();
    videoInfo->bmiHeader.biHeight = format.height();
    videoInfo->bmiHeader.biPlanes = 1;
    videoInfo->bmiHeader.biBitCount = WORD(format.bpp());
    videoInfo->bmiHeader.biCompression = compressionFromFormat(format.fourcc());
    videoInfo->bmiHeader.biSizeImage = DWORD(format.size());

    switch (videoInfo->bmiHeader.biCompression) {
    case BI_RGB:
        if (videoInfo->bmiHeader.biBitCount == 8) {
            videoInfo->bmiHeader.biClrUsed = iPALETTE_COLORS;

            if (HDC hdc = GetDC(nullptr)) {
                PALETTEENTRY palette[iPALETTE_COLORS];

                if (GetSystemPaletteEntries(hdc,
                                            0,
                                            iPALETTE_COLORS,
                                            palette))
                    for (int i = 0; i < iPALETTE_COLORS; i++) {
                        videoInfo->TrueColorInfo.bmiColors[i].rgbRed = palette[i].peRed;
                        videoInfo->TrueColorInfo.bmiColors[i].rgbBlue = palette[i].peBlue;
                        videoInfo->TrueColorInfo.bmiColors[i].rgbGreen = palette[i].peGreen;
                        videoInfo->TrueColorInfo.bmiColors[i].rgbReserved = 0;
                    }

                ReleaseDC(nullptr, hdc);
            }
        }

        break;

    case BI_BITFIELDS: {
            auto masks = VideoFormatSpecsPrivate::byPixelFormat(format.fourcc())->masks;

            if (masks)
                memcpy(videoInfo->TrueColorInfo.dwBitMasks, masks, 3);
        }

        break;

    default:
        break;
    }

    auto mediaType =
            reinterpret_cast<AM_MEDIA_TYPE *>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    memset(mediaType, 0, sizeof(AM_MEDIA_TYPE));

    // Initialize media type.
    mediaType->majortype = MEDIATYPE_Video;
    mediaType->subtype = subtype;
    mediaType->bFixedSizeSamples = TRUE;
    mediaType->bTemporalCompression = FALSE;
    mediaType->lSampleSize = ULONG(frameSize);
    mediaType->formattype = FORMAT_VideoInfo;
    mediaType->cbFormat = sizeof(VIDEOINFO);
    mediaType->pbFormat = reinterpret_cast<BYTE *>(videoInfo);

    return mediaType;
}

AkVCam::VideoFormat AkVCam::formatFromMediaType(const AM_MEDIA_TYPE *mediaType)
{
    if (!mediaType)
        return VideoFormat();

    if (!IsEqualGUID(mediaType->majortype, MEDIATYPE_Video))
        return VideoFormat();

    if (!isSubTypeSupported(mediaType->subtype))
        return VideoFormat();

    if (!mediaType->pbFormat)
        return VideoFormat();

    if (IsEqualGUID(mediaType->formattype, FORMAT_VideoInfo)) {
        auto format = reinterpret_cast<VIDEOINFOHEADER *>(mediaType->pbFormat);
        auto fps = Fraction {uint32_t(TIME_BASE),
                             uint32_t(format->AvgTimePerFrame)};

        return VideoFormat(formatFromGuid(mediaType->subtype),
                           format->bmiHeader.biWidth,
                           std::abs(format->bmiHeader.biHeight),
                           {fps});
    } else if (IsEqualGUID(mediaType->formattype, FORMAT_VideoInfo2)) {
        auto format = reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType->pbFormat);
        auto fps = Fraction {uint32_t(TIME_BASE),
                             uint32_t(format->AvgTimePerFrame)};

        return VideoFormat(formatFromGuid(mediaType->subtype),
                           format->bmiHeader.biWidth,
                           std::abs(format->bmiHeader.biHeight),
                           {fps});
    }

    return VideoFormat();
}

bool AkVCam::isEqualMediaType(const AM_MEDIA_TYPE *mediaType1,
                              const AM_MEDIA_TYPE *mediaType2,
                              bool exact)
{
    if (mediaType1 == mediaType2)
        return true;

    if (!mediaType1 || !mediaType2)
        return false;

    if (!IsEqualGUID(mediaType1->majortype, mediaType2->majortype)
        || !IsEqualGUID(mediaType1->subtype, mediaType2->subtype)
        || !IsEqualGUID(mediaType1->formattype, mediaType2->formattype))
        return false;

    if (mediaType1->pbFormat == mediaType2->pbFormat)
        return true;

    if (exact)
        return memcmp(mediaType1->pbFormat,
                      mediaType2->pbFormat,
                      mediaType1->cbFormat) == 0;

    if (IsEqualGUID(mediaType1->formattype, FORMAT_VideoInfo)) {
        auto format1 = reinterpret_cast<VIDEOINFOHEADER *>(mediaType1->pbFormat);
        auto format2 = reinterpret_cast<VIDEOINFOHEADER *>(mediaType2->pbFormat);

        if (format1->bmiHeader.biWidth == format2->bmiHeader.biWidth
            && format1->bmiHeader.biHeight == format2->bmiHeader.biHeight)
            return true;
    } else if (IsEqualGUID(mediaType1->formattype, FORMAT_VideoInfo2)) {
        auto format1 = reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType1->pbFormat);
        auto format2 = reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType2->pbFormat);

        if (format1->bmiHeader.biWidth == format2->bmiHeader.biWidth
            && format1->bmiHeader.biHeight == format2->bmiHeader.biHeight)
            return true;
    }

    return false;
}

bool AkVCam::copyMediaType(AM_MEDIA_TYPE *dstMediaType,
                           const AM_MEDIA_TYPE *srcMediaType)
{
    if (!dstMediaType)
        return false;

    if (!srcMediaType) {
        memset(dstMediaType, 0, sizeof(AM_MEDIA_TYPE));

        return false;
    }

    memcpy(dstMediaType, srcMediaType, sizeof(AM_MEDIA_TYPE));

    if (dstMediaType->cbFormat && dstMediaType->pbFormat) {
        dstMediaType->pbFormat =
                reinterpret_cast<BYTE *>(CoTaskMemAlloc(dstMediaType->cbFormat));
        memcpy(dstMediaType->pbFormat,
               srcMediaType->pbFormat,
               dstMediaType->cbFormat);
    }

    return true;
}

AM_MEDIA_TYPE *AkVCam::createMediaType(const AM_MEDIA_TYPE *mediaType)
{
    if (!mediaType)
        return nullptr;

    auto newMediaType =
            reinterpret_cast<AM_MEDIA_TYPE *>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    memcpy(newMediaType, mediaType, sizeof(AM_MEDIA_TYPE));

    if (newMediaType->cbFormat && newMediaType->pbFormat) {
        newMediaType->pbFormat =
                reinterpret_cast<BYTE *>(CoTaskMemAlloc(newMediaType->cbFormat));
        memcpy(newMediaType->pbFormat,
               mediaType->pbFormat,
               newMediaType->cbFormat);
    }

    return newMediaType;
}

void AkVCam::deleteMediaType(AM_MEDIA_TYPE **mediaType)
{
    if (!mediaType || !*mediaType)
        return;

    auto format = (*mediaType)->pbFormat;

    if (format && (*mediaType)->cbFormat)
        CoTaskMemFree(format);

    CoTaskMemFree(*mediaType);
    *mediaType = nullptr;
}

bool AkVCam::containsMediaType(const AM_MEDIA_TYPE *mediaType,
                               IEnumMediaTypes *mediaTypes)
{
    AM_MEDIA_TYPE *mt = nullptr;
    mediaTypes->Reset();
    auto isEqual = false;

    while (mediaTypes->Next(1, &mt, nullptr) == S_OK) {
        isEqual = isEqualMediaType(mt, mediaType);
        deleteMediaType(&mt);

        if (isEqual)
            break;
    }

    return isEqual;
}

std::string AkVCam::stringFromMajorType(const GUID &majorType)
{
    static const std::map<GUID, std::string> mtToStr {
        {GUID_NULL              , "GUID_NULL"              },
        {MEDIATYPE_AnalogAudio  , "MEDIATYPE_AnalogAudio"  },
        {MEDIATYPE_AnalogVideo  , "MEDIATYPE_AnalogVideo"  },
        {MEDIATYPE_Audio        , "MEDIATYPE_Audio"        },
        {MEDIATYPE_AUXLine21Data, "MEDIATYPE_AUXLine21Data"},
        {MEDIATYPE_File         , "MEDIATYPE_File"         },
        {MEDIATYPE_Interleaved  , "MEDIATYPE_Interleaved"  },
        {MEDIATYPE_LMRT         , "MEDIATYPE_LMRT"         },
        {MEDIATYPE_Midi         , "MEDIATYPE_Midi"         },
        {MEDIATYPE_MPEG2_PES    , "MEDIATYPE_MPEG2_PES"    },
        {MEDIATYPE_ScriptCommand, "MEDIATYPE_ScriptCommand"},
        {MEDIATYPE_Stream       , "MEDIATYPE_Stream"       },
        {MEDIATYPE_Text         , "MEDIATYPE_Text"         },
        {MEDIATYPE_Timecode     , "MEDIATYPE_Timecode"     },
        {MEDIATYPE_URL_STREAM   , "MEDIATYPE_URL_STREAM"   },
        {MEDIATYPE_VBI          , "MEDIATYPE_VBI"          },
        {MEDIATYPE_Video        , "MEDIATYPE_Video"        }
    };

    for (auto &mediaType: mtToStr)
        if (IsEqualGUID(mediaType.first, majorType))
            return mediaType.second;

    return stringFromIid(majorType);
}

std::string AkVCam::stringFromSubType(const GUID &subType)
{
    static const std::map<GUID, std::string> mstToStr {
        {GUID_NULL               , "GUID_NULL"               },
        {MEDIASUBTYPE_RGB1       , "MEDIASUBTYPE_RGB1"       },
        {MEDIASUBTYPE_RGB4       , "MEDIASUBTYPE_RGB4"       },
        {MEDIASUBTYPE_RGB8       , "MEDIASUBTYPE_RGB8"       },
        {MEDIASUBTYPE_RGB555     , "MEDIASUBTYPE_RGB555"     },
        {MEDIASUBTYPE_RGB565     , "MEDIASUBTYPE_RGB565"     },
        {MEDIASUBTYPE_RGB24      , "MEDIASUBTYPE_RGB24"      },
        {MEDIASUBTYPE_RGB32      , "MEDIASUBTYPE_RGB32"      },
        {MEDIASUBTYPE_ARGB1555   , "MEDIASUBTYPE_ARGB1555"   },
        {MEDIASUBTYPE_ARGB32     , "MEDIASUBTYPE_ARGB32"     },
        {MEDIASUBTYPE_ARGB4444   , "MEDIASUBTYPE_ARGB4444"   },
        {MEDIASUBTYPE_A2R10G10B10, "MEDIASUBTYPE_A2R10G10B10"},
        {MEDIASUBTYPE_A2B10G10R10, "MEDIASUBTYPE_A2B10G10R10"},
        {MEDIASUBTYPE_AYUV       , "MEDIASUBTYPE_AYUV"       },
        {MEDIASUBTYPE_YUY2       , "MEDIASUBTYPE_YUY2"       },
        {MEDIASUBTYPE_UYVY       , "MEDIASUBTYPE_UYVY"       },
        {MEDIASUBTYPE_IMC1       , "MEDIASUBTYPE_IMC1"       },
        {MEDIASUBTYPE_IMC3       , "MEDIASUBTYPE_IMC3"       },
        {MEDIASUBTYPE_IMC2       , "MEDIASUBTYPE_IMC2"       },
        {MEDIASUBTYPE_IMC4       , "MEDIASUBTYPE_IMC4"       },
        {MEDIASUBTYPE_YV12       , "MEDIASUBTYPE_YV12"       },
        {MEDIASUBTYPE_NV12       , "MEDIASUBTYPE_NV12"       },
        {MEDIASUBTYPE_IF09       , "MEDIASUBTYPE_IF09"       },
        {MEDIASUBTYPE_IYUV       , "MEDIASUBTYPE_IYUV"       },
        {MEDIASUBTYPE_Y211       , "MEDIASUBTYPE_Y211"       },
        {MEDIASUBTYPE_Y411       , "MEDIASUBTYPE_Y411"       },
        {MEDIASUBTYPE_Y41P       , "MEDIASUBTYPE_Y41P"       },
        {MEDIASUBTYPE_YVU9       , "MEDIASUBTYPE_YVU9"       },
        {MEDIASUBTYPE_YVYU       , "MEDIASUBTYPE_YVYU"       }
    };

    for (auto &mediaType: mstToStr)
        if (IsEqualGUID(mediaType.first, subType))
            return mediaType.second;

    return stringFromIid(subType);
}

std::string AkVCam::stringFromFormatType(const GUID &formatType)
{
    static const std::map<GUID, std::string> ftToStr {
        {GUID_NULL          , "GUID_NULL"          },
        {FORMAT_DvInfo      , "FORMAT_DvInfo"      },
        {FORMAT_MPEG2Video  , "FORMAT_MPEG2Video"  },
        {FORMAT_MPEGStreams , "FORMAT_MPEGStreams" },
        {FORMAT_MPEGVideo   , "FORMAT_MPEGVideo"   },
        {FORMAT_None        , "FORMAT_None"        },
        {FORMAT_VideoInfo   , "FORMAT_VideoInfo"   },
        {FORMAT_VideoInfo2  , "FORMAT_VideoInfo2"  },
        {FORMAT_WaveFormatEx, "FORMAT_WaveFormatEx"}
    };

    for (auto &mediaType: ftToStr)
        if (IsEqualGUID(mediaType.first, formatType))
            return mediaType.second;

    return stringFromIid(formatType);
}

std::string AkVCam::stringFromMediaType(const AM_MEDIA_TYPE *mediaType)
{
    if (!mediaType)
        return std::string("MediaType(NULL)");

    std::stringstream ss;
    ss << "MediaType("
       << stringFromMajorType(mediaType->majortype)
       << ", "
       << stringFromSubType(mediaType->subtype)
       << ", "
       << stringFromFormatType(mediaType->formattype);

    if (IsEqualGUID(mediaType->formattype, FORMAT_VideoInfo)) {
        auto format = reinterpret_cast<VIDEOINFOHEADER *>(mediaType->pbFormat);
        ss << ", "
           << format->bmiHeader.biWidth
           << ", "
           << format->bmiHeader.biHeight;
    } else if (IsEqualGUID(mediaType->formattype, FORMAT_VideoInfo2)) {
        auto format = reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType->pbFormat);
        ss << ", "
           << format->bmiHeader.biWidth
           << ", "
           << format->bmiHeader.biHeight;
    }

    ss << ")";

    return ss.str();
}

std::string AkVCam::stringFromMediaSample(IMediaSample *mediaSample)
{
    if (!mediaSample)
        return std::string("MediaSample(NULL)");

    BYTE *buffer = nullptr;
    mediaSample->GetPointer(&buffer);
    auto bufferSize = mediaSample->GetSize();
    AM_MEDIA_TYPE *mediaType = nullptr;
    mediaSample->GetMediaType(&mediaType);
    REFERENCE_TIME timeStart = 0;
    REFERENCE_TIME timeEnd = 0;
    mediaSample->GetTime(&timeStart, &timeEnd);
    REFERENCE_TIME mediaTimeStart = 0;
    REFERENCE_TIME mediaTimeEnd = 0;
    mediaSample->GetMediaTime(&mediaTimeStart, &mediaTimeEnd);
    auto discontinuity = mediaSample->IsDiscontinuity() == S_OK;
    auto preroll = mediaSample->IsPreroll() == S_OK;
    auto syncPoint = mediaSample->IsSyncPoint() == S_OK;
    auto dataLength = mediaSample->GetActualDataLength();

    std::stringstream ss;
    ss << "MediaSample(" << std::endl
       << "    Buffer: " << size_t(buffer) << std::endl
       << "    Buffer Size: " << bufferSize << std::endl
       << "    Media Type: " << stringFromMediaType(mediaType) << std::endl
       << "    Time: (" << timeStart << ", " << timeEnd << ")" << std::endl
       << "    Media Time: (" << mediaTimeStart << ", " << mediaTimeEnd << ")" << std::endl
       << "    Discontinuity: " << discontinuity << std::endl
       << "    Preroll: " << preroll << std::endl
       << "    Sync Point: " << syncPoint << std::endl
       << "    Data Length: " << dataLength << std::endl
       << ")";

    deleteMediaType(&mediaType);

    return ss.str();
}

LSTATUS AkVCam::deleteTree(HKEY key, LPCSTR subkey, REGSAM samFlags)
{
    HKEY mainKey = key;
    LONG result = ERROR_SUCCESS;

    // Open subkey
    if (subkey) {
        result = RegOpenKeyExA(key,
                               subkey,
                               0,
                               KEY_ALL_ACCESS | samFlags,
                               &mainKey);

        if (result != ERROR_SUCCESS)
            return result;
    }

    // list subkeys and values.
    DWORD subKeys = 0;
    DWORD maxSubKeyLen = 0;
    DWORD values = 0;
    DWORD maxValueNameLen = 0;
    result = RegQueryInfoKey(mainKey,
                             nullptr,
                             nullptr,
                             nullptr,
                             &subKeys,
                             &maxSubKeyLen,
                             nullptr,
                             &values,
                             &maxValueNameLen,
                             nullptr,
                             nullptr,
                             nullptr);

    if (result != ERROR_SUCCESS) {
        RegCloseKey(mainKey);

        return result;
    }

    // Delete subkeys
    for (DWORD i = 0; i < subKeys; i++) {
        auto len = maxSubKeyLen + 1;
        CHAR *name = new CHAR[len];
        memset(name, 0, len * sizeof(CCHAR));
        DWORD nameLen = len;
        result = RegEnumKeyExA(mainKey,
                               i,
                               name,
                               &nameLen,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr);

        if (result == ERROR_SUCCESS)
            deleteTree(mainKey, name, samFlags);

        delete [] name;
    }

    // Delete values
    for (DWORD i = 0; i < values; i++) {
        auto len = maxValueNameLen + 1;
        TCHAR *name = new TCHAR[len];
        memset(name, 0, len * sizeof(TCHAR));
        DWORD nameLen = len;
        result = RegEnumValue(mainKey,
                              i,
                              name,
                              &nameLen,
                              0,
                              nullptr,
                              nullptr,
                              nullptr);

        if (result == ERROR_SUCCESS)
            RegDeleteValue(mainKey, name);

        delete [] name;
    }

    // Delete this key
    if (subkey) {
        result = RegDeleteKeyExA(key, subkey, samFlags, 0);
        RegCloseKey(mainKey);
    }

    return result;
}

LSTATUS AkVCam::copyTree(HKEY src, LPCSTR subkey, HKEY dst, REGSAM samFlags)
{
    HKEY hkeyFrom = src;
    LONG result = ERROR_SUCCESS;

    // Open source subkey
    if (subkey) {
        result = RegOpenKeyExA(src,
                               subkey,
                               0,
                               KEY_READ | samFlags,
                               &hkeyFrom);

        if (result != ERROR_SUCCESS)
            return result;
    }

    // list subkeys and values.
    DWORD subKeys = 0;
    DWORD maxSubKeyLen = 0;
    DWORD values = 0;
    DWORD maxValueNameLen = 0;
    DWORD maxValueLen = 0;
    result = RegQueryInfoKey(hkeyFrom,
                             nullptr,
                             nullptr,
                             nullptr,
                             &subKeys,
                             &maxSubKeyLen,
                             nullptr,
                             &values,
                             &maxValueNameLen,
                             &maxValueLen,
                             nullptr,
                             nullptr);

    if (result != ERROR_SUCCESS) {
        if (subkey)
            RegCloseKey(hkeyFrom);

        return result;
    }

    // Copy subkeys
    for (DWORD i = 0; i < subKeys; i++) {
        auto len = maxSubKeyLen + 1;
        CHAR *name = new CHAR[len];
        memset(name, 0, len * sizeof(CCHAR));
        DWORD nameLen = len;
        result = RegEnumKeyExA(hkeyFrom,
                               i,
                               name,
                               &nameLen,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr);

        if (result == ERROR_SUCCESS) {
            HKEY subkeyTo = nullptr;
            result = RegCreateKeyExA(dst,
                                     name,
                                     0,
                                     nullptr,
                                     REG_OPTION_NON_VOLATILE,
                                     KEY_WRITE | samFlags,
                                     nullptr,
                                     &subkeyTo,
                                     nullptr);

            if (result == ERROR_SUCCESS) {
                copyTree(hkeyFrom, name, subkeyTo, samFlags);
                RegCloseKey(subkeyTo);
            }
        }

        delete [] name;
    }

    // Copy values
    for (DWORD i = 0; i < values; i++) {
        auto len = maxValueNameLen + 1;
        TCHAR *name = new TCHAR[len];
        memset(name, 0, len * sizeof(TCHAR));
        DWORD nameLen = len;
        DWORD dataType = 0;
        BYTE *data = new BYTE[maxValueLen];
        DWORD dataSize = 0;
        result = RegEnumValue(hkeyFrom,
                              i,
                              name,
                              &nameLen,
                              0,
                              &dataType,
                              data,
                              &dataSize);

        if (result == ERROR_SUCCESS)
            RegSetValueEx(dst,
                          name,
                          0,
                          dataType,
                          data,
                          dataSize);

        delete [] data;
        delete [] name;
    }

    if (subkey)
        RegCloseKey(hkeyFrom);

    return result;
}
