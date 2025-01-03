/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include <igsioCommon.h>
#include <MWCapture.h>
#include "MWCaptureAPIWrapper.h"


//----------------------------------------------------------------------------
int DeckLinkAPIWrapper::PixelFormatFromString(const std::string& _arg)
{
    if (igsioCommon::IsEqualInsensitive(_arg, "GREY")) { return MWFOURCC_GREY; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "Y800")) { return MWFOURCC_Y800; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "Y8")) { return MWFOURCC_Y8; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "Y16")) { return MWFOURCC_Y16; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "RGB5")) { return MWFOURCC_RGB15; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "RGB6")) { return MWFOURCC_RGB16; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "RGB")) { return MWFOURCC_RGB24; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "RGBA")) { return MWFOURCC_RGBA; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "ARGB")) { return MWFOURCC_ARGB; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "BGR5")) { return MWFOURCC_BGR15; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "BGR6")) { return MWFOURCC_BGR16; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "BGR")) { return MWFOURCC_BGR24; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "BGRA")) { return MWFOURCC_BGRA; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "ABGR")) { return MWFOURCC_ABGR; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "NV16")) { return MWFOURCC_NV16; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "NV61")) { return MWFOURCC_NV61; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "I422")) { return MWFOURCC_I422; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "YUY2")) { return MWFOURCC_YUY2; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "YUYV")) { return MWFOURCC_YUYV; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "UYVY")) { return MWFOURCC_UYVY; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "YVYU")) { return MWFOURCC_YVYU; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "VYUY")) { return MWFOURCC_VYUY; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "I420")) { return MWFOURCC_I420; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "IYUV")) { return MWFOURCC_IYUV; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "NV12")) { return MWFOURCC_NV12; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "YV12")) { return MWFOURCC_YV12; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "NV21")) { return MWFOURCC_NV21; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "P010")) { return MWFOURCC_P010; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "P210")) { return MWFOURCC_P210; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "IYU2")) { return MWFOURCC_IYU2; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "V308")) { return MWFOURCC_V308; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "AYUV")) { return MWFOURCC_AYUV; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "UYVA")) { return MWFOURCC_UYVA; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "V408")) { return MWFOURCC_V408; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "VYUA")) { return MWFOURCC_VYUA; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "V210")) { return MWFOURCC_V210; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "Y410")) { return MWFOURCC_Y410; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "V410")) { return MWFOURCC_V410; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "RG10")) { return MWFOURCC_RGB10; }
    else if (igsioCommon::IsEqualInsensitive(_arg, "BG10")) { return MWFOURCC_BGR10; }
    else { return MWFOURCC_UNK; }
}