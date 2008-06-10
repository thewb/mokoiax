//
// iaxclient: a cross-platform IAX softphone library
//
// Copyrights:
// Copyright (C) 2006, Horizon Wimba, Inc.
// Copyright (C) 2007, Wimba, Inc.
//
// Contributors:
// Mihai Balea <mihai AT hates DOT ms>
// Peter Grayson <jpgrayson@gmail.com>
// Bill Cholewka <bcholew@gmail.com>
//
// This program is free software, distributed under the terms of
// the GNU Lesser (Library) General Public License.
//

//------------------------------------------------------------------------------
// File: wingrab.cpp
//
// Desc: DirectShow class
//       This class will connect to a video capture device,
//       create a filter graph with a sample grabber filter,
//       and read a frame out of it to a memory location.
// Author: Francesco Delfino pluto@tipic.com
//
// Based on:
// File:   GrabBitmaps.cpp
// Author: Audrey J.W. Mbogho walegwa@yahoo.com
//------------------------------------------------------------------------------

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>  // For _CrtSetReportMode
#include <atlbase.h>
#include <qedit.h>
#include <comutil.h>
#include <DShow.h>
#include <wxdebug.h>
#include <fourcc.h>
#include "video_grab.h"

class CSampleGrabberCB : public ISampleGrabberCB
{
public:
	// These will get set by the main thread below. We need to
	// know this in order to write out the bmp
	long width;
	long height;
	unsigned int imageFormat;
	grab_callback_t grab_callback;
	void *grab_callback_data;
	CComQIPtr< IMediaControl, &IID_IMediaControl > pControl;

	// Fake out any COM ref counting
	//
	STDMETHODIMP_(ULONG) AddRef() { return 2; }
	STDMETHODIMP_(ULONG) Release() { return 1; }

	// Fake out any COM QI'ing
	//
	STDMETHODIMP QueryInterface(REFIID riid, void ** ppv)
	{
		CheckPointer(ppv,E_POINTER);

		if( riid == IID_ISampleGrabberCB || riid == IID_IUnknown )
		{
			*ppv = (void *) static_cast<ISampleGrabberCB*> ( this );
			return NOERROR;
		}

		return E_NOINTERFACE;
	}

	// We don't implement this one
	//
	STDMETHODIMP SampleCB( double SampleTime, IMediaSample * pSample )
	{
		return S_OK;
	}


	// The sample grabber is calling us back on its deliver thread.
	// This is NOT the main app thread!
	//
	STDMETHODIMP BufferCB( double dblSampleTime, BYTE * pBuffer, long lBufferSize )
	{
		grab_callback( grab_callback_data, dblSampleTime,
				(void *)pBuffer,
				width * height * 3 / 2);
		return S_OK;
	}

	void MyFreeMediaType(AM_MEDIA_TYPE& mt)
	{
	    if ( mt.cbFormat != 0 )
	        CoTaskMemFree((PVOID)mt.pbFormat);

	    if ( mt.pUnk != NULL )
	        mt.pUnk->Release();
	}

	AM_MEDIA_TYPE *GetMediaType( CComPtr< IPin > pPin, bool allowAlternates )
	{
		CComPtr< IEnumMediaTypes > pEnum;

		HRESULT hr = pPin->EnumMediaTypes (&pEnum);

		if ( FAILED(hr) )
			return NULL;

		ULONG ulFound;
		AM_MEDIA_TYPE * pMedia;

		while ( S_OK == pEnum->Next(1, &pMedia, &ulFound) )
		{
			VIDEOINFOHEADER * vih =
				(VIDEOINFOHEADER *) pMedia->pbFormat;

				fprintf(stderr, "%d x %d 0x%x  ",
					vih->bmiHeader.biWidth,
					vih->bmiHeader.biHeight,
					pMedia->subtype.Data1);

			if ( vih->bmiHeader.biWidth == width &&
			     vih->bmiHeader.biHeight == height &&
			     (pMedia->subtype.Data1 == IMAGE_FORMAT_I420 ||
			      pMedia->subtype.Data1 == IMAGE_FORMAT_IYUV) )
			{
				imageFormat = pMedia->subtype.Data1;
				if ( imageFormat == IMAGE_FORMAT_I420 )
					fprintf(stderr, "- Using this image format: I420.\n");
				else
					fprintf(stderr, "- Using this image format: IYUV.\n");
				return pMedia;
			} else
			{
				// Support for alternate image format: YUY2
				if ( allowAlternates && vih->bmiHeader.biWidth == width &&
				     vih->bmiHeader.biHeight == height &&
				     pMedia->subtype.Data1 == IMAGE_FORMAT_YUY2 )
				{
					imageFormat = IMAGE_FORMAT_YUY2;
					fprintf(stderr, "- Using this image format: YUY2.\n");
					return pMedia;
				}
				MyFreeMediaType(*pMedia);
				fprintf(stderr, "- Rejecting this format.\n");
			}
		}

		return NULL;
	}

	int listMediaTypes( CComPtr< IPin > pPin, char * strBuff, int buffSize )
	{
		CComPtr< IEnumMediaTypes > pEnum;

		HRESULT hr = pPin->EnumMediaTypes (&pEnum);

		if ( FAILED(hr) )
			return 1;

		ULONG ulFound;
		AM_MEDIA_TYPE * pMedia;

		while ( S_OK == pEnum->Next(1, &pMedia, &ulFound) )
		{
			VIDEOINFOHEADER * vih =
				(VIDEOINFOHEADER *) pMedia->pbFormat;

			const int strSize = 128;
			char formatStr[strSize];
			sprintf_s(formatStr, strSize, "%d x %d  0x%x\n",
				vih->bmiHeader.biWidth,
				vih->bmiHeader.biHeight,
				pMedia->subtype.Data1);
			strcat_s(strBuff, buffSize, formatStr);

			MyFreeMediaType(*pMedia);
		}

		return 0;
	}

	void sprintDeviceInfo(IMoniker * pM, IBindCtx * pbc, char* strBuff, int buffSize)
	{
		USES_CONVERSION;

		HRESULT hr;

		CComPtr< IMalloc > pMalloc;

		hr = CoGetMalloc(1, &pMalloc);

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed CoGetMalloc\n");
			return;
		}

		LPOLESTR pDisplayName;

		hr = pM->GetDisplayName(pbc, 0, &pDisplayName);

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed GetDisplayName\n");
			return;
		}

		// This gets stack memory, no dealloc needed.
		char * pszDisplayName = OLE2A(pDisplayName);

		pMalloc->Free(pDisplayName);

		CComPtr< IPropertyBag > pPropBag;

		hr = pM->BindToStorage(pbc, 0, IID_IPropertyBag,
				(void **)&pPropBag);

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed getting video device property bag\n");
			return;
		}

		VARIANT v;
		VariantInit(&v);

		char * pszFriendlyName = 0;

		hr = pPropBag->Read(L"FriendlyName", &v, 0);

		if ( SUCCEEDED(hr) )
			pszFriendlyName =
				_com_util::ConvertBSTRToString(
						v.bstrVal);

		sprintf_s(strBuff, buffSize, "\nDevice: \"%s\"\nDriver: \"%s\"\n",
				pszFriendlyName, pszDisplayName);

		delete [] pszFriendlyName;
	}

	int inventoryVideoCaptureDevices(char *charBuff, int buffSize)
	{
		HRESULT hr;
		if ( buffSize > 0 )
			charBuff[0] = NULL;
		else
			return 1;

		// Create an enumerator
		CComPtr<ICreateDevEnum> pCreateDevEnum;

		pCreateDevEnum.CoCreateInstance(CLSID_SystemDeviceEnum);

		if ( !pCreateDevEnum )
		{
			strcat_s(charBuff, buffSize, "Failed creating device enumerator\n");
			return 1;
		}

		// Enumerate video capture devices
		CComPtr<IEnumMoniker> pEm;

		pCreateDevEnum->CreateClassEnumerator(
				CLSID_VideoInputDeviceCategory, &pEm, 0);

		if ( !pEm )
		{
			strcat_s(charBuff, buffSize, "Failed creating enumerator moniker\n");
			return 1;
		}

		pEm->Reset();

		ULONG ulFetched;
		IMoniker * pM;

		while ( pEm->Next(1, &pM, &ulFetched) == S_OK )
		{
			CComPtr< IBindCtx > pbc;

			hr = CreateBindCtx(0, &pbc);

			if ( FAILED(hr) )
			{
				strcat_s(charBuff, buffSize, "failed CreateBindCtx\n");
				return 1;
			}

			const int strSize = 1024;
			char devInfoString[strSize];
			sprintDeviceInfo(pM, pbc, devInfoString, strSize);
			strcat_s(charBuff, buffSize, devInfoString);

			IBaseFilter * pCaptureFilter = 0;
			CComPtr<IPin> pOutPin = 0;

			hr = pM->BindToObject(pbc, 0, IID_IBaseFilter,
					(void **)&pCaptureFilter);

			if ( FAILED(hr) )
			{
				strcat_s(charBuff, buffSize, "failed BindToObject\n");
				goto clean_continue;
			}

			pOutPin = GetOutPin( pCaptureFilter, 0 );

			if ( !pOutPin )
			{
				strcat_s(charBuff, buffSize, "Failed to get source output pin\n");
				goto clean_continue;
			}

			strcat_s(charBuff, buffSize, "Image formats:\n");

			char mediaTypeString[strSize];
			mediaTypeString[0] = NULL;
			int rc = listMediaTypes(pOutPin, mediaTypeString, strSize);

			strcat_s(charBuff, buffSize, mediaTypeString);

clean_continue:
			if ( pCaptureFilter )
				pCaptureFilter->Release();

			if ( pOutPin )
				pOutPin.Release();

			pM->Release();
		}

		return 0;
	}

	bool GetDefaultCapDevice(IBaseFilter **ppCaptureFilter,
			IPin ** ppOutPin, AM_MEDIA_TYPE ** ppMediaType,
			bool allowAlternates)
	{
		HRESULT hr;

		if (!ppCaptureFilter)
			return false;

		*ppCaptureFilter = 0;
		*ppOutPin = 0;
		*ppMediaType = 0;

		// Create an enumerator
		CComPtr<ICreateDevEnum> pCreateDevEnum;

		pCreateDevEnum.CoCreateInstance(CLSID_SystemDeviceEnum);

		if ( !pCreateDevEnum )
		{
			fprintf(stderr, "failed creating device enumerator\n");
			return false;
		}

		// Enumerate video capture devices
		CComPtr<IEnumMoniker> pEm;

		pCreateDevEnum->CreateClassEnumerator(
				CLSID_VideoInputDeviceCategory, &pEm, 0);

		if ( !pEm )
		{
			fprintf(stderr, "failed creating enumerator moniker\n");
			return false;
		}

		pEm->Reset();

		ULONG ulFetched;
		IMoniker * pM;

		while ( pEm->Next(1, &pM, &ulFetched) == S_OK )
		{
			CComPtr< IBindCtx > pbc;

			hr = CreateBindCtx(0, &pbc);

			if ( FAILED(hr) )
			{
				fprintf(stderr, "failed CreateBindCtx\n");
				return false;
			}

			int strSize = 512;
			char *devInfoString = new char[strSize];
			sprintDeviceInfo(pM, pbc, devInfoString, strSize);
			fprintf(stderr, "%s", devInfoString);

			IBaseFilter * pCaptureFilter = 0;
			CComPtr<IPin> pOutPin = 0;

			hr = pM->BindToObject(pbc, 0, IID_IBaseFilter,
					(void **)&pCaptureFilter);

			if ( FAILED(hr) )
			{
				fprintf(stderr, "failed BindToObject\n");
				goto clean_continue;
			}

			pOutPin = GetOutPin( pCaptureFilter, 0 );

			if ( !pOutPin )
			{
				fprintf(stderr, "failed getting source output pin\n");
				goto clean_continue;
			}

			AM_MEDIA_TYPE * pMediaType = GetMediaType(pOutPin, allowAlternates);

			if ( pMediaType )
			{
				pM->Release();
				*ppCaptureFilter = pCaptureFilter;
				*ppOutPin = pOutPin;
				*ppMediaType = pMediaType;
				return true;
			}

			fprintf(stderr, "did not match media type\n");

clean_continue:
			if ( pCaptureFilter )
				pCaptureFilter->Release();

			if ( pOutPin )
				pOutPin.Release();

			pM->Release();
		}

		return false;
	}

	HRESULT GetPin( IBaseFilter * pFilter, PIN_DIRECTION dirrequired,
			int iNum, IPin **ppPin)
	{
		*ppPin = NULL;

		CComPtr< IEnumPins > pEnum;
		HRESULT hr = pFilter->EnumPins(&pEnum);

		if ( FAILED(hr) )
			return hr;

		ULONG ulFound;
		IPin * pPin;
		hr = E_FAIL;

		while ( S_OK == pEnum->Next(1, &pPin, &ulFound) )
		{
			PIN_DIRECTION pindir;

			pPin->QueryDirection(&pindir);

			if ( pindir == dirrequired )
			{
				if ( iNum == 0 )
				{
					*ppPin = pPin;
					hr = S_OK;
					break;
				}
				iNum--;
			}

			pPin->Release();
		}

		return hr;
	}

	IPin * GetInPin( IBaseFilter * pFilter, int nPin )
	{
		CComPtr<IPin> pComPin = 0;
		GetPin(pFilter, PINDIR_INPUT, nPin, &pComPin);
		return pComPin;
	}

	IPin * GetOutPin( IBaseFilter * pFilter, int nPin )
	{
		CComPtr<IPin> pComPin = 0;
		GetPin(pFilter, PINDIR_OUTPUT, nPin, &pComPin);
		return pComPin;
	}

	int Init( grab_callback_t grab_callback, void *grab_callback_data, int w, int h)
	{
		USES_CONVERSION;

		this->grab_callback = grab_callback;
		this->grab_callback_data = grab_callback_data;
		this->width = w;
		this->height = h;

		// Create the sample grabber
		//
		CComPtr< ISampleGrabber > pGrabber;
		pGrabber.CoCreateInstance( CLSID_SampleGrabber );
		if ( !pGrabber )
		{
			fprintf(stderr, "failed instantiating sample grabber\n");
			return 0;
		}
		CComQIPtr< IBaseFilter, &IID_IBaseFilter > pGrabberBase(pGrabber);

		// Get whatever capture device exists
		//
		IBaseFilter * pSourceTmp;
		IPin * pSourceOutPinTmp;
		AM_MEDIA_TYPE * pMediaType;

		// First pass: look for I420 / IYUV formats
		if ( !GetDefaultCapDevice(&pSourceTmp, &pSourceOutPinTmp,
					&pMediaType, false) )
		{
			// Second pass: look for YUY2
			fprintf(stderr, "Expanding search for supported image formats.\n");
			if ( !GetDefaultCapDevice(&pSourceTmp, &pSourceOutPinTmp,
						&pMediaType, true) )
				return 0;
		}

		// Put the results into smart pointers so the references to
		// these objects are tracked properly.
		CComPtr< IBaseFilter > pSource(pSourceTmp);
		CComPtr< IPin > pSourceOutPin(pSourceOutPinTmp);

		CComPtr< IBaseFilter > pNullRenderer;
		pNullRenderer.CoCreateInstance( CLSID_NullRenderer );
		if ( !pNullRenderer )
		{
			fprintf(stderr, "failed instantiating null renderer\n");
			return 0;
		}

		// Create the graph
		//
		CComPtr< IGraphBuilder > pGraph;
		pGraph.CoCreateInstance( CLSID_FilterGraph );
		if ( !pGraph )
		{
			fprintf(stderr, "failed instantiating filter graph\n");
			return 0;
		}

		HRESULT hr;

		// Put them in the graph
		//
		hr = pGraph->AddFilter( pSource, L"Source" );

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed adding source filter\n");
			return 0;
		}

		hr = pGraph->AddFilter( pGrabberBase, L"Grabber" );

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed adding grabber filter\n");
			return 0;
		}

		hr = pGraph->AddFilter( pNullRenderer, L"NullRenderer" );

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed adding null renderer filter\n");
			return 0;
		}

		CComPtr< IPin > pGrabPin = GetInPin( pGrabberBase, 0 );

		if ( !pGrabPin )
		{
			fprintf(stderr, "failed to get input pin\n");
			return 0;
		}

		CComPtr< IPin > pGrabOutPin = GetOutPin( pGrabberBase, 0 );

		if ( !pGrabOutPin )
		{
			fprintf(stderr, "failed to get grabber out pin\n");
			return 0;
		}

		CComPtr< IPin > pNullRendererInPin = GetInPin( pNullRenderer, 0 );

		if ( !pNullRendererInPin )
		{
			fprintf(stderr, "failed to get null renderer input pin\n");
			return 0;
		}

		hr = pGrabber->SetMediaType( pMediaType );

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed to set media type\n");
			return 0;
		}

		// Don't buffer the samples as they pass through
		//
		hr = pGrabber->SetBufferSamples( FALSE );

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed SetBufferSamples\n");
			return 0;
		}

		// Only grab one at a time, stop stream after
		// grabbing one sample
		//
		hr = pGrabber->SetOneShot( FALSE );

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed SetOneShot\n");
			return 0;
		}

		// Set the callback, so we can grab the one sample
		//
		hr = pGrabber->SetCallback( this, 1 );

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed to set callback\n");
			return 0;
		}

		fflush(stderr);

		// ... and connect them
		//
		hr = pGraph->ConnectDirect( pSourceOutPin, pGrabPin, pMediaType);

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed to connect source and grabber\n");
			return 0;
		}

		hr = pGraph->ConnectDirect( pGrabOutPin, pNullRendererInPin, pMediaType);

		if ( FAILED(hr) )
		{
			fprintf(stderr, "failed to connect grabber to null renderer %d\n", hr);
			return 0;
		}

		MyFreeMediaType(*pMediaType);

		// activate the threads
		pControl = CComQIPtr<IMediaControl, &IID_IMediaControl>(pGraph);
		pControl->Run();
		return TRUE;
	}

	void Destroy()
	{
		if (pControl)
		{
			pControl->Stop();
			pControl.Release();
			width = 0;
			height = 0;
			grab_callback = 0;
			grab_callback_data = 0;
		}
	}
};

static CSampleGrabberCB CB;

_invalid_parameter_handler oldHandler, newHandler;

void myInvalidParameterHandler(const wchar_t* expression,
	const wchar_t* function,
	const wchar_t* file,
	unsigned int line,
	uintptr_t pReserved)
{
	wprintf(L"Invalid parameter detected in function %s."
		L" File: %s Line: %d\n", function, file, line);
	wprintf(L"Expression: %s\n", expression);
}

int listVidCapDevices(char *buff, int buffSize)
{
	newHandler = myInvalidParameterHandler;
	oldHandler = _set_invalid_parameter_handler(newHandler);

	// Disable the message box for assertions.
	_CrtSetReportMode(_CRT_ASSERT, 0);

	return CB.inventoryVideoCaptureDevices(buff, buffSize);
}

int grabber_format_check(unsigned int format)
{
	return CB.imageFormat == format;
}

void *grabber_init( grab_callback_t grab_callback, void *grab_callback_data,
		int w, int h)
{
	HRESULT hr = CoInitialize(NULL);

	if ( FAILED(hr) )
	{
		fprintf(stderr, "failed CoInitialize\n");
		return 0;
	}

	grabber_destroy(NULL);

	if (CB.Init(grab_callback, grab_callback_data, w, h))
	{
		return &CB;
	}
	else
	{
		fprintf(stderr, "sample grabber init failed\n");
		return 0;
	}
}

void grabber_destroy( void *opaque )
{
	CB.Destroy();
}

