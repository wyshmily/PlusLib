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

// STL includes
#include <atomic>

/*!
\class vtkPlusMWCaptureVideoSource
\brief Interface to a Magewell capture card
\ingroup PlusLibDataCollection
*/
class vtkPlusDataCollectionExport vtkPlusMWCaptureVideoSource : public vtkPlusDevice
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
    // IUnknown interface
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv);
    virtual ULONG STDMETHODCALLTYPE AddRef();
    virtual ULONG STDMETHODCALLTYPE Release();

private:
    static void MWCaptureVideoCallback(BYTE* p_buffer, int frame_len, UINT64 ts, void* p_param);
    void VideoInputFrameArrived(BYTE* p_buffer, int frame_len, int ts);

protected:
    std::atomic<ULONG> ReferenceCount;

private:
    vtkPlusMWCaptureVideoSource(const vtkPlusMWCaptureVideoSource&); // Not implemented
    void operator=(const vtkPlusMWCaptureVideoSource&); // Not implemented

    class vtkInternal;
    vtkInternal* Internal;

};

#endif
