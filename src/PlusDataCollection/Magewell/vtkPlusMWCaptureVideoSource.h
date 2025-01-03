/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __vtkPlusMWCaptureVideoSource_h
#define __vtkPlusMWCaptureVideoSource_h

#include "vtkPlusDataCollectionExport.h"
#include "vtkPlusDevice.h"

// MWCapture includes
#if WIN32
  // Windows includes
#include <comutil.h>
#endif
#include <DeckLinkAPI.h>

// STL includes
#include <atomic>

/*!
\class vtkPlusMWCaptureVideoSource
\brief Interface to a BlackMagic MWCapture capture card
\ingroup PlusLibDataCollection
*/
class vtkPlusDataCollectionExport vtkPlusMWCaptureVideoSource : public vtkPlusDevice, public IDeckLinkInputCallback
{
public:
    static vtkPlusMWCaptureVideoSource* New();
    vtkTypeMacro(vtkPlusMWCaptureVideoSource, vtkPlusDevice);
    void PrintSelf(ostream& os, vtkIndent indent);

    /* Device is a hardware tracker. */
    virtual bool IsTracker() const;
    virtual bool IsVirtual() const;

    virtual PlusStatus ReadConfiguration(vtkXMLDataElement* config);
    virtual PlusStatus WriteConfiguration(vtkXMLDataElement* config);
    virtual PlusStatus InternalConnect();
    virtual PlusStatus InternalDisconnect();
    virtual PlusStatus InternalStartRecording();
    virtual PlusStatus InternalStopRecording();
    virtual PlusStatus Probe();
    virtual PlusStatus NotifyConfigured();

protected:
    vtkPlusMWCaptureVideoSource();
    ~vtkPlusMWCaptureVideoSource();

protected:
    // IMWCaptureInputCallback interface
    virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode* newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags);
    virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioPacket);

    // IUnknown interface
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv);
    virtual ULONG STDMETHODCALLTYPE AddRef();
    virtual ULONG STDMETHODCALLTYPE Release();

protected:
    std::atomic<ULONG> ReferenceCount;

private:
    vtkPlusMWCaptureVideoSource(const vtkPlusMWCaptureVideoSource&); // Not implemented
    void operator=(const vtkPlusMWCaptureVideoSource&); // Not implemented

    class vtkInternal;
    vtkInternal* Internal;

};

#endif
