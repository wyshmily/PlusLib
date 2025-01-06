/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

// Local includes
#include "vtkPlusDataSource.h"
#include "vtkPlusMWCaptureVideoSource.h"
#include <PixelCodec.h>

// VTK includes
#include <vtkImageAppendComponents.h>
#include <vtkImageExtractComponents.h>
#include <vtkImageImport.h>
#include <vtkObject.h>

// System includes
#include <string>

// MWCapture SDK includes
#include <MWCapture.h>
#include "MWCaptureAPIWrapper.h"

//----------------------------------------------------------------------------
// vtkPlusMWCaptureVideoSource::vtkInternal
//----------------------------------------------------------------------------

class vtkPlusMWCaptureVideoSource::vtkInternal : public vtkObject
{
public:
    static vtkPlusMWCaptureVideoSource::vtkInternal* New(vtkPlusMWCaptureVideoSource*);
    vtkTypeMacro(vtkInternal, vtkObject);

public:
    vtkPlusMWCaptureVideoSource* External;

    vtkInternal(vtkPlusMWCaptureVideoSource* external)
        : External(external) {
    }

    virtual ~vtkInternal() {}

    FrameSizeType             RequestedFrameSize = { 1920, 1080, 1 };
    std::string               RequestedPixelFormatName = "YUY2";
    int                       RequestedPixelFormat = MWFOURCC_YUY2;
    std::string               RequestedDeviceFamily = "USB Capture";
    int DeviceIndex = -1;
    std::string DeviceName = "";

    int MWChannelIndex = -1;
    HCHANNEL MWChannelHandle = nullptr;
    HANDLE MWVideoCaptureHandle = nullptr;
    MWCAP_VIDEO_SIGNAL_STATUS* MWInputSignalStatus = nullptr;

    unsigned char* RGBBytes = nullptr;
    unsigned char* GrayBytes = nullptr;

private:
    static vtkPlusMWCaptureVideoSource::vtkInternal* New();
    vtkInternal() : External(nullptr) {}
};

namespace
{
    //----------------------------------------------------------------------------
    bool AreSame(double a, double b)
    {
        return std::abs(a - b) < 0.0001;
    }

    //----------------------------------------------------------------------------
    bool InitCOM(std::atomic_bool& comInit)
    {
#if WIN32
        if (!comInit)
        {
            // Initialize COM on this thread
            HRESULT result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            if (FAILED(result))
            {
                LOG_ERROR("Initialization of COM failed - result = " << std::hex << std::setw(8) << std::setfill('0') << result);
                return false;
            }
            comInit = true;
        }
#endif
        return true;
    }

    //----------------------------------------------------------------------------
    void ShutdownCOM(std::atomic_bool& comInit)
    {
        if (comInit)
        {
            CoUninitialize();
            comInit = false;
        }
    }
}
//----------------------------------------------------------------------------
// vtkPlusMWCaptureVideoSource
//----------------------------------------------------------------------------

vtkStandardNewMacro(vtkPlusMWCaptureVideoSource);
vtkStandardNewMacro(vtkPlusMWCaptureVideoSource::vtkInternal);

//----------------------------------------------------------------------------
vtkPlusMWCaptureVideoSource::vtkPlusMWCaptureVideoSource::vtkInternal* vtkPlusMWCaptureVideoSource::vtkInternal::New(vtkPlusMWCaptureVideoSource* _arg)
{
    vtkPlusMWCaptureVideoSource::vtkInternal* result = new vtkPlusMWCaptureVideoSource::vtkInternal();
    result->InitializeObjectBase();
    result->External = _arg;
    return result;
}

//----------------------------------------------------------------------------
vtkPlusMWCaptureVideoSource::vtkPlusMWCaptureVideoSource()
    : vtkPlusDevice()
    , Internal(vtkInternal::New(this))
    , ReferenceCount(1)
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::vtkPlusMWCaptureVideoSource()");

    this->FrameNumber = 0;
    this->StartThreadForInternalUpdates = false; // callback based device
}

//----------------------------------------------------------------------------
vtkPlusMWCaptureVideoSource::~vtkPlusMWCaptureVideoSource()
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::~vtkPlusMWCaptureVideoSource()");

    if (this->Internal->MWInputSignalStatus != nullptr) {
        delete this->Internal->MWInputSignalStatus;
        this->Internal->MWInputSignalStatus = nullptr;
    }

    this->Internal->Delete();
    this->Internal = nullptr;
}



//----------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE vtkPlusMWCaptureVideoSource::QueryInterface(REFIID iid, LPVOID* ppv)
{
    HRESULT result = E_NOINTERFACE;

    if (ppv == NULL)
    {
        return E_INVALIDARG;
    }

    // Initialize the return result
    *ppv = NULL;

    // Obtain the IUnknown interface and compare it the provided REFIID
    if (iid == IID_IUnknown)
    {
        *ppv = this;
        AddRef();
        result = S_OK;
    }

    return result;
}

//----------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE vtkPlusMWCaptureVideoSource::AddRef()
{
    return ++ReferenceCount;
}

//----------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE vtkPlusMWCaptureVideoSource::Release()
{
    ULONG newRefValue;

    ReferenceCount--;
    newRefValue = ReferenceCount;
    if (newRefValue == 0)
    {
        delete this;
        return 0;
    }

    return newRefValue;
}

//----------------------------------------------------------------------------
void vtkPlusMWCaptureVideoSource::PrintSelf(ostream& os, vtkIndent indent)
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::PrintSelf(ostream& os, vtkIndent indent)");
    Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
bool vtkPlusMWCaptureVideoSource::IsTracker() const
{
    return false;
}

//----------------------------------------------------------------------------
bool vtkPlusMWCaptureVideoSource::IsVirtual() const
{
    return false;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::ReadConfiguration");

    XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

    XML_FIND_NESTED_ELEMENT_REQUIRED(dataSourcesElement, deviceConfig, "DataSources");

    int devIndex(-1);
    XML_READ_SCALAR_ATTRIBUTE_NONMEMBER_OPTIONAL(int, DeviceIndex, devIndex, deviceConfig);
    if (devIndex != -1)
    {
        this->Internal->DeviceIndex = devIndex;
    }

    std::string devName("");
    XML_READ_STRING_ATTRIBUTE_NONMEMBER_OPTIONAL(DeviceName, devName, deviceConfig);
    if (!devName.empty())
    {
        this->Internal->DeviceName = devName;
    }

    std::string devFamily("");
    XML_READ_STRING_ATTRIBUTE_NONMEMBER_OPTIONAL(DeviceFamily, devFamily, deviceConfig);
    if (!devName.empty())
    {
        this->Internal->RequestedDeviceFamily = devFamily;
    }

    int size[3] = { -1, -1, -1 };
    XML_READ_VECTOR_ATTRIBUTE_NONMEMBER_OPTIONAL(int, 2, FrameSize, size, deviceConfig);
    if (size[0] != -1 && size[1] != -1)
    {
        this->Internal->RequestedFrameSize[0] = static_cast<unsigned int>(size[0]);
        this->Internal->RequestedFrameSize[1] = static_cast<unsigned int>(size[1]);
        this->Internal->RequestedFrameSize[2] = 1;
    }

    std::string pixelFormat("");
    XML_READ_STRING_ATTRIBUTE_NONMEMBER_OPTIONAL(PixelFormat, pixelFormat, deviceConfig);
    if (!pixelFormat.empty())
    {
        this->Internal->RequestedPixelFormat = DeckLinkAPIWrapper::PixelFormatFromString(pixelFormat);
        if (this->Internal->RequestedPixelFormat != MWFOURCC_YUY2)
        {
            LOG_ERROR("Unsupported pixel format requested. Currently YUY2 is the only supported capture pixel format");
            return PLUS_FAIL;
        }
    }

    return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::WriteConfiguration");
    XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(deviceConfig, rootConfigElement);
    return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::InternalConnect()
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::InternalConnect");

    if (!MWCaptureInitInstance()) {
        LOG_TRACE("failed to init MWCapture SDK");
        goto out;
    }

    if (MW_RESULT::MW_SUCCEEDED != MWRefreshDevice()) {
        LOG_TRACE("failed to scan MWCapture devices");
        goto out;
    }

    int channelCount = MWGetChannelCount();
    if (channelCount == 0) {
        LOG_TRACE("no MWCapture device found");
        goto out;
    }

    MWCAP_CHANNEL_INFO channelInfo;
    MW_RESULT rt = MW_RESULT::MW_FAILED;
    int count = 0;
    for (int channelIndex = 0; channelIndex < channelCount; channelIndex++) {

        if (this->Internal->DeviceIndex == -1 || count == this->Internal->DeviceIndex)
        {
            rt = MWGetChannelInfoByIndex(channelIndex, &channelInfo);
            if (rt != MW_RESULT::MW_SUCCEEDED)
            {
                LOG_ERROR("Could not obtain the MWCAP_CHANNEL_INFO - result = " << std::hex << std::setw(8) << std::setfill('0') << rt);
                goto out;
            }

            // first match the requested device family by name
            if (this->Internal->RequestedDeviceFamily._Equal(channelInfo.szFamilyName)) {

                // then match the requested pixel format and frame size
                WCHAR path[128] = { 0 };
                MWGetDevicePath(channelIndex, path);
                HCHANNEL channelHandle = MWOpenChannelByPath(path);
                if (NULL == channelHandle) {
                    LOG_ERROR("failed to open channel - channel index = " << channelIndex);
                    goto out;
                }

                int formatCount = 0;
                if (!MWGetVideoCaptureSupportFormat(channelHandle, NULL, &formatCount)) {
                    LOG_ERROR("failed to get MWCapture channel support format count - channel index = " << channelIndex);
                    goto out;
                }

                VIDEO_FORMAT_INFO* p_format = (VIDEO_FORMAT_INFO*)malloc(formatCount * sizeof(VIDEO_FORMAT_INFO));

                if (!MWGetVideoCaptureSupportFormat(channelHandle, p_format, &formatCount)) {
                    if (p_format) {
                        free(p_format);
                    }
                    LOG_ERROR("failed to get MWCapture channel support formats - channel index = " << channelIndex);
                    goto out;
                }

                for (int i = 0; i < formatCount; i++) {
                    if (this->Internal->RequestedPixelFormatName._Equal(p_format[i].colorSpace)
                        && this->Internal->RequestedFrameSize[0] == p_format[i].cx
                        && this->Internal->RequestedFrameSize[1] == p_format[i].cy) {

                        // check signal staus
                        MWCAP_VIDEO_SIGNAL_STATUS* signalStatus = new MWCAP_VIDEO_SIGNAL_STATUS;
                        MWGetVideoSignalStatus(channelHandle, signalStatus);
                        switch (signalStatus->state)
                        {
                        case MWCAP_VIDEO_SIGNAL_NONE:
                            LOG_INFO("Input signal status: NONE - channel index = " << channelIndex);
                            break;
                        case MWCAP_VIDEO_SIGNAL_UNSUPPORTED:
                            LOG_INFO("Input signal status: Unsupported - channel index = " << channelIndex);
                            break;
                        case MWCAP_VIDEO_SIGNAL_LOCKING:
                            LOG_INFO("Input signal status: Locking - channel index = " << channelIndex);
                            break;
                        case MWCAP_VIDEO_SIGNAL_LOCKED:
                            LOG_INFO("Input signal status: Locked - channel index = " << channelIndex);
                            break;
                        }

                        if (signalStatus->state == MWCAP_VIDEO_SIGNAL_LOCKED || signalStatus->state == MWCAP_VIDEO_SIGNAL_NONE) {
                            this->Internal->MWChannelIndex = channelIndex;
                            this->Internal->MWChannelHandle = channelHandle;
                            this->Internal->MWInputSignalStatus = signalStatus;
                            break;
                        }
                        else {
                            delete signalStatus;
                        }
                    }
                }

                free(p_format);

                if (this->Internal->MWChannelHandle) {
                    goto out;
                }
            }
        }
    }
    LOG_ERROR("Unable to locate requested capture parameters.")

        out:
    if (this->Internal->MWInputSignalStatus)
    {
        vtkPlusDataSource* source;
        this->GetFirstVideoSource(source);
        source->SetInputFrameSize(this->Internal->RequestedFrameSize);
        source->SetPixelType(VTK_UNSIGNED_CHAR);

        if (source->GetImageType() == US_IMG_RGB_COLOR)
        {
            source->SetNumberOfScalarComponents(3);
            this->Internal->RGBBytes = (unsigned char*)malloc(this->Internal->RequestedFrameSize[0] * this->Internal->RequestedFrameSize[1] * 3 * sizeof(unsigned char));
        }
        else
        {
            source->SetNumberOfScalarComponents(1);
            this->Internal->GrayBytes = (unsigned char*)malloc(this->Internal->RequestedFrameSize[0] * this->Internal->RequestedFrameSize[1] * 1 * sizeof(unsigned char));
        }


        return PLUS_SUCCESS;
    }

    return PLUS_FAIL;
}


//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::InternalDisconnect()
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::InternalDisconnect");

    free(this->Internal->RGBBytes);
    free(this->Internal->GrayBytes);

    delete this->Internal->MWInputSignalStatus;
    this->Internal->MWInputSignalStatus = nullptr;

    this->StopRecording();

    return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::InternalStartRecording()
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::InternalStartRecording");

    if (this->Internal->MWChannelHandle != nullptr)
    {
        DWORD frameDuration = this->Internal->MWInputSignalStatus->dwFrameDuration;
        HANDLE handle = MWCreateVideoCapture(this->Internal->MWChannelHandle, this->Internal->RequestedFrameSize[0], this->Internal->RequestedFrameSize[1],
            this->Internal->RequestedPixelFormat, frameDuration, vtkPlusMWCaptureVideoSource::MWCaptureVideoCallback, this);
        if (NULL == handle) {
            LOG_ERROR("Unable to create video capture - channel index = " << this->Internal->MWChannelIndex);
            delete this->Internal->MWInputSignalStatus;
            this->Internal->MWInputSignalStatus = nullptr;
            MWCloseChannel(this->Internal->MWChannelHandle);
            this->Internal->MWChannelHandle = nullptr;
            this->Internal->MWChannelIndex = -1;
            return PLUS_FAIL;
        }
        this->Internal->MWVideoCaptureHandle = handle;
    }
    else
    {
        return PLUS_FAIL;
    }

    return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::InternalStopRecording()
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::InternalStopRecording");

    if (this->Internal->MWVideoCaptureHandle != nullptr)
    {
        if (MW_RESULT::MW_SUCCEEDED != MWDestoryVideoCapture(this->Internal->MWVideoCaptureHandle)) {
            return PLUS_FAIL;
        }
    }
    else {
        return PLUS_FAIL;
    }

    return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::Probe()
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::Probe");

    // Do stuff
    bool rt = MWCaptureInitInstance();
    if (!rt) {
        return PLUS_FAIL;
    }

    if (MW_RESULT::MW_SUCCEEDED != MWRefreshDevice()) {
        return PLUS_FAIL;
    }

    int channelCount = MWGetChannelCount();
    if (channelCount == 0) {
        return PLUS_FAIL;
    }

    return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::NotifyConfigured()
{
    if (this->GetNumberOfVideoSources() == 0)
    {
        LOG_ERROR("MWCaptureVideoSource requires at least one video source. Please correct configuration file.");
        return PLUS_FAIL;
    }

    if (this->OutputChannelCount() == 0)
    {
        LOG_ERROR("MWCaptureVideoSource requires at least one output channel. Please correct configuration file.");
        return PLUS_FAIL;
    }

    return PLUS_SUCCESS;
}


//----------------------------------------------------------------------------

void vtkPlusMWCaptureVideoSource::MWCaptureVideoCallback(BYTE* p_buffer, int frame_len, UINT64 ts, void* p_param) {
    vtkPlusMWCaptureVideoSource* thisClass = (vtkPlusMWCaptureVideoSource*)p_param;
    if (thisClass)
        thisClass->VideoInputFrameArrived(p_buffer, frame_len, ts);
}

void STDMETHODCALLTYPE vtkPlusMWCaptureVideoSource::VideoInputFrameArrived(BYTE* p_buffer, int frame_len, int ts)
{
    if (!this->IsRecording()) {
        return;
    }

    vtkPlusDataSource* source;
    this->GetFirstVideoSource(source);


    if (source->GetImageType() == US_IMG_RGB_COLOR)
    {
        // Flip BGRA to RGB
        PixelCodec::YUV422pToRGB24(PixelCodec::ComponentOrder_RGB, this->Internal->RequestedFrameSize[0], this->Internal->RequestedFrameSize[1], p_buffer, this->Internal->RGBBytes);
    }
    else
    {
        PixelCodec::YUV422pToGray(this->Internal->RequestedFrameSize[0], this->Internal->RequestedFrameSize[1], p_buffer, this->Internal->GrayBytes);
    }

    if (source->AddItem(source->GetImageType() == US_IMG_RGB_COLOR ? this->Internal->RGBBytes : this->Internal->GrayBytes,
        source->GetInputImageOrientation(),
        this->Internal->RequestedFrameSize,
        VTK_UNSIGNED_CHAR, source->GetNumberOfScalarComponents(),
        source->GetImageType(),
        0,
        this->FrameNumber) != PLUS_SUCCESS)
    {
        LOG_ERROR("Unable to add video item to buffer.");
        return;
    }
    this->FrameNumber++;
}