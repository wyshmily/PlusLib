/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

// Local includes
#include "PlusOutputVideoFrame.h"
#include "vtkIGSIOAccurateTimer.h"
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
#include "DeckLinkAPIWrapper.h"

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

    int                       DeviceIndex = -1;
    bool                      PreviousFrameValid = false;
    std::string               DeviceName = "";
    FrameSizeType             RequestedFrameSize = { 1920, 1080, 1 };
    BMDPixelFormat            RequestedPixelFormat = bmdFormatUnspecified;
    BMDVideoConnection        RequestedVideoConnection = bmdVideoConnectionUnspecified;
    BMDDisplayMode            RequestedDisplayMode = bmdModeUnknown;

    IDeckLink* MWCapture = nullptr;
    IDeckLinkInput* MWCaptureInput = nullptr;
    IDeckLinkDisplayMode* MWCaptureDisplayMode = nullptr;
    IDeckLinkVideoConversion* MWCaptureVideoConversion = nullptr;

    PlusOutputVideoFrame* OutputFrame = nullptr;

    unsigned char* RGBBytes = nullptr;
    unsigned char* GrayBytes = nullptr;

    std::atomic_bool          COMInitialized = false;

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

    if (this->Internal->MWCaptureDisplayMode != nullptr)
    {
        this->Internal->MWCaptureDisplayMode->Release();
    }
    if (this->Internal->MWCaptureInput != nullptr)
    {
        this->Internal->MWCaptureInput->Release();
    }
    if (this->Internal->MWCapture != nullptr)
    {
        this->Internal->MWCapture->Release();
    }

    ShutdownCOM(this->Internal->COMInitialized);

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
    else if (iid == IID_IDeckLinkInputCallback)
    {
        *ppv = (IDeckLinkInputCallback*)this;
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
        if (this->Internal->RequestedPixelFormat == bmdFormatUnspecified)
        {
            LOG_ERROR("Unknown pixel format requested. Please see device page documentation for supported pixel formats.");
            return PLUS_FAIL;
        }
    }

    std::string videoConnection("");
    XML_READ_STRING_ATTRIBUTE_NONMEMBER_OPTIONAL(VideoConnection, videoConnection, deviceConfig);
    if (!videoConnection.empty())
    {
        this->Internal->RequestedVideoConnection = DeckLinkAPIWrapper::VideoConnectionFromString(videoConnection);
        if (this->Internal->RequestedVideoConnection == bmdVideoConnectionUnspecified)
        {
            LOG_ERROR("Unknown connection type requested. Please see device page documentation for supported connections.");
            return PLUS_FAIL;
        }
    }

    std::string displayMode("");
    XML_READ_STRING_ATTRIBUTE_NONMEMBER_OPTIONAL(DisplayMode, displayMode, deviceConfig);
    if (!displayMode.empty())
    {
        this->Internal->RequestedDisplayMode = DeckLinkAPIWrapper::DisplayModeFromString(displayMode);
        if (this->Internal->RequestedDisplayMode == bmdModeUnknown)
        {
            LOG_ERROR("Unable to recognize requested display mode. Please see device documentation for valid entries.");
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

    IDeckLink* deckLink(nullptr);
    IDeckLinkIterator* deckLinkIterator(nullptr);
    IDeckLinkInput* deckLinkInput(nullptr);
    IDeckLinkDisplayModeIterator* deckLinkDisplayModeIterator(nullptr);
    IDeckLinkDisplayMode* deckLinkDisplayMode(nullptr);
    HRESULT result;

    if (!InitCOM(this->Internal->COMInitialized))
    {
        goto out;
    }

    this->Internal->MWCaptureVideoConversion = DeckLinkAPIWrapper::CreateVideoConversion();

    deckLinkIterator = DeckLinkAPIWrapper::CreateDeckLinkIterator();

    // Enumerate all cards in this system
    int count = 0;
    while (deckLinkIterator->Next(&deckLink) == S_OK)
    {
        if (this->Internal->DeviceIndex == -1 || count == this->Internal->DeviceIndex)
        {
            // Query the MWCapture for its input interface
            result = deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&deckLinkInput);
            if (result != S_OK)
            {
                LOG_ERROR("Could not obtain the IMWCaptureInput interface - result = " << std::hex << std::setw(8) << std::setfill('0') << result);
                goto out;
            }

            if (deckLinkInput->GetDisplayModeIterator(&deckLinkDisplayModeIterator) != S_OK)
            {
                LOG_ERROR("Unable to iterate display modes. Cannot select input display mode.");
                return PLUS_FAIL;
            }

            while (deckLinkDisplayModeIterator->Next(&deckLinkDisplayMode) == S_OK)
            {
                if (this->Internal->RequestedDisplayMode != bmdModeUnknown && deckLinkDisplayMode->GetDisplayMode() == this->Internal->RequestedDisplayMode)
                {
                    BOOL supported;
                    BMDDisplayMode actualMode;
                    if (deckLinkInput->DoesSupportVideoMode(this->Internal->RequestedVideoConnection, this->Internal->RequestedDisplayMode, this->Internal->RequestedPixelFormat, BMDVideoInputConversionMode::bmdNoVideoInputConversion, bmdSupportedVideoModeDefault, &actualMode, &supported) == S_OK && supported)
                    {
                        // Found by display mode
                        this->Internal->MWCapture = deckLink;
                        this->Internal->MWCaptureInput = deckLinkInput;
                        this->Internal->MWCaptureDisplayMode = deckLinkDisplayMode;
                        goto out;
                    }
                }
                else if (this->Internal->RequestedDisplayMode == bmdModeUnknown)
                {
                    BMDTimeValue frameDuration;
                    BMDTimeScale timeScale;
                    if (deckLinkDisplayMode->GetFrameRate(&frameDuration, &timeScale) != S_OK)
                    {
                        LOG_WARNING("Unable to retrieve frame rate for display mode. Skipping.");
                        continue;
                    }

                    double frameRate = (double)timeScale / (double)frameDuration;
                    if (deckLinkDisplayMode->GetWidth() == this->Internal->RequestedFrameSize[0] &&
                        deckLinkDisplayMode->GetHeight() == this->Internal->RequestedFrameSize[1] &&
                        AreSame(frameRate, this->AcquisitionRate))
                    {
                        BOOL supported;
                        BMDDisplayMode actualMode;
                        if (deckLinkInput->DoesSupportVideoMode(this->Internal->RequestedVideoConnection, this->Internal->RequestedDisplayMode, this->Internal->RequestedPixelFormat, BMDVideoInputConversionMode::bmdNoVideoInputConversion, bmdSupportedVideoModeDefault, &actualMode, &supported) == S_OK && supported)
                        {
                            // Found by frame details
                            this->Internal->MWCapture = deckLink;
                            this->Internal->MWCaptureInput = deckLinkInput;
                            this->Internal->MWCaptureDisplayMode = deckLinkDisplayMode;
                            goto out;
                        }
                    }
                }
                deckLinkDisplayMode->Release();
            }

            deckLinkDisplayModeIterator->Release();
            deckLinkInput->Release();
            deckLink->Release();
        }

        count++;
    }

    LOG_ERROR("Unable to locate requested capture parameters.")

        out:
    if (deckLinkDisplayModeIterator)
    {
        deckLinkDisplayModeIterator->Release();
    }
    if (deckLinkIterator)
    {
        deckLinkIterator->Release();
    }

    if (this->Internal->MWCaptureInput)
    {
        // Confirm data source is correctly configured. If not, abort
        BMDTimeValue frameDuration;
        BMDTimeScale timeScale;
        this->Internal->MWCaptureDisplayMode->GetFrameRate(&frameDuration, &timeScale);
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

        this->Internal->MWCaptureInput->SetCallback(this);
        this->Internal->MWCaptureInput->SetScreenPreviewCallback(nullptr);
        this->Internal->MWCaptureInput->DisableAudioInput();

        HRESULT res = this->Internal->MWCaptureInput->EnableVideoInput(this->Internal->MWCaptureDisplayMode->GetDisplayMode(), this->Internal->RequestedPixelFormat, bmdVideoInputFlagDefault);
        if (res != S_OK)
        {
            LPTSTR errorMsgPtr = 0;
            FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, res, 0, (LPTSTR)&errorMsgPtr, 0, NULL);
            LOG_ERROR("Unable to convert video frame: " << errorMsgPtr);
            LocalFree(errorMsgPtr);
            this->Internal->MWCaptureDisplayMode->Release();
            this->Internal->MWCaptureDisplayMode = nullptr;
            this->Internal->MWCaptureInput->Release();
            this->Internal->MWCaptureInput = nullptr;
            this->Internal->MWCapture->Release();
            this->Internal->MWCapture = nullptr;
            return PLUS_FAIL;
        }

        this->Internal->OutputFrame = new PlusOutputVideoFrame(this->Internal->MWCaptureDisplayMode->GetWidth(), this->Internal->MWCaptureDisplayMode->GetHeight(), bmdFormat8BitBGRA, bmdFrameFlagDefault);
    }

    return this->Internal->MWCaptureInput == nullptr ? PLUS_FAIL : PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::InternalDisconnect()
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::InternalDisconnect");

    free(this->Internal->RGBBytes);
    free(this->Internal->GrayBytes);

    this->Internal->MWCaptureVideoConversion->Release();
    this->Internal->MWCaptureVideoConversion = nullptr;

    if (this->Internal->OutputFrame != nullptr)
    {
        delete this->Internal->OutputFrame;
        this->Internal->OutputFrame = nullptr;
    }

    this->StopRecording();
    this->Internal->MWCaptureInput->SetScreenPreviewCallback(NULL);
    this->Internal->MWCaptureInput->SetCallback(NULL);

    if (this->Internal->MWCaptureDisplayMode != nullptr)
    {
        this->Internal->MWCaptureDisplayMode->Release();
        this->Internal->MWCaptureDisplayMode = nullptr;
    }
    if (this->Internal->MWCaptureInput != nullptr)
    {
        this->Internal->MWCaptureInput->Release();
        this->Internal->MWCaptureInput = nullptr;
    }
    if (this->Internal->MWCapture != nullptr)
    {
        this->Internal->MWCapture->Release();
        this->Internal->MWCapture = nullptr;
    }

    ShutdownCOM(this->Internal->COMInitialized);

    return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::InternalStartRecording()
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::InternalStartRecording");

    if (this->Internal->MWCaptureInput != nullptr)
    {
        if (this->Internal->MWCaptureInput->StartStreams() != S_OK)
        {
            return PLUS_FAIL;
        }
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

    if (this->Internal->MWCaptureInput != nullptr)
    {
        if (this->Internal->MWCaptureInput->StopStreams() != S_OK)
        {
            return PLUS_FAIL;
        }
    }
    else
    {
        return PLUS_FAIL;
    }

    return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMWCaptureVideoSource::Probe()
{
    LOG_TRACE("vtkPlusMWCaptureVideoSource::Probe");

    bool comInit = this->Internal->COMInitialized;
    if (!InitCOM(this->Internal->COMInitialized))
    {
        return PLUS_FAIL;
    }

    // Do stuff
    IDeckLinkIterator* deckLinkIterator = DeckLinkAPIWrapper::CreateDeckLinkIterator();
    IDeckLink* deckLink(nullptr);

    if (deckLinkIterator == nullptr)
    {
        return PLUS_FAIL;
    }

    // Does this system contain any MWCapture device?
    bool result(false);
    if (deckLinkIterator->Next(&deckLink) == S_OK)
    {
        result = true;
    }

    if (!comInit)
    {
        ShutdownCOM(this->Internal->COMInitialized);
    }

    return result ? PLUS_SUCCESS : PLUS_FAIL;
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
HRESULT STDMETHODCALLTYPE vtkPlusMWCaptureVideoSource::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode* newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags)
{
    return S_OK;
}

//----------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE vtkPlusMWCaptureVideoSource::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioPacket)
{
    if (!this->IsRecording())
    {
        return S_OK;
    }

    if (videoFrame)
    {
        this->Internal->OutputFrame->SetFlags(videoFrame->GetFlags());

        bool inputFrameValid = ((videoFrame->GetFlags() & bmdFrameHasNoInputSource) == 0);

        if (inputFrameValid && !this->Internal->PreviousFrameValid)
        {
            this->Internal->MWCaptureInput->StopStreams();
            this->Internal->MWCaptureInput->FlushStreams();
            this->Internal->MWCaptureInput->StartStreams();
        }

        if (inputFrameValid)
        {
            HRESULT res = this->Internal->MWCaptureVideoConversion->ConvertFrame(videoFrame, this->Internal->OutputFrame);
            if (res != S_OK)
            {
                LPTSTR errorMsgPtr = 0;
                FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, res, 0, (LPTSTR)&errorMsgPtr, 0, NULL);
                LOG_ERROR("Unable to convert video frame: " << errorMsgPtr);
                LocalFree(errorMsgPtr);
            }

            if (this->Internal->PreviousFrameValid && res == S_OK)
            {
                void* buffer;
                if (this->Internal->OutputFrame->GetBytes(&buffer) == S_OK)
                {
                    vtkPlusDataSource* source;
                    this->GetFirstVideoSource(source);

                    if (source->GetImageType() == US_IMG_RGB_COLOR)
                    {
                        // Flip BGRA to RGB
                        PixelCodec::BGRA32ToRGB24(this->Internal->RequestedFrameSize[0], this->Internal->RequestedFrameSize[1], (unsigned char*)buffer, this->Internal->RGBBytes);
                    }
                    else
                    {
                        PixelCodec::RGBA32ToGray(this->Internal->RequestedFrameSize[0], this->Internal->RequestedFrameSize[1], (unsigned char*)buffer, this->Internal->GrayBytes);
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
                        return PLUS_FAIL;
                    }
                    this->FrameNumber++;
                }
            }
        }

        this->Internal->PreviousFrameValid = inputFrameValid;
    }
    return S_OK;
}
