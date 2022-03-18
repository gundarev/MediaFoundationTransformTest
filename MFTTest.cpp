// MIT License
// 
// Copyright 2018 Otto Itkonen
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


// This code demonstrates the use of a hardware H.264 encoder MFT.
// No useful encoding is done, but encoder performance is measured under
// different rates of incoming samples. You may need to modify the sample
// for use with a specific hardware encoder, as it makes a few assumptions
// about the MFT.

// The sample will print performance data to the console in the following format,
// one data point per encoded frame.
// [running time in seconds] [encode time in ms] [capped framerate?]

// Press down F8 (uncapKey) to enable uncapped framerate, i.e. to feed
// a sample into the encoder as soon as the last sample finishes encoding
// instead of adhering to the set framerate (frameRate).


#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Winmm.lib")

// std
#include <iostream>
#include <string>

// Windows
#include <windows.h>
#include <atlbase.h>

// DirectX
#include <d3d11.h>

// Media Foundation
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <Codecapi.h>
#include <dshow.h>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <ks.h>
#include <stdio.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wmcodecdsp.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <wmcodecdsp.h>

// Simplified error handling via macros
#ifdef DEBUG
#define ON_ERROR __debugbreak(); return 1;
#else
#define ON_ERROR system("pause"); return 1;
#endif

#define CHECK(x, err) if (!(x)) { std::cerr << err << std::endl; ON_ERROR; }
#define CHECK_HR(x, err) if (FAILED(x)) { std::cerr << err << std::endl; ON_ERROR; }

// Constants
const int frameRate = 60;
const int surfaceWidth = 1920;
const int surfaceHeight = 1080;
const int uncapKey = VK_F8;
const int quitKey = VK_F9;


int main()
{
	HRESULT hr;

	CComPtr<ID3D11Device> device;
	CComPtr<ID3D11DeviceContext> context;
	CComPtr<IMFDXGIDeviceManager> deviceManager;
	CComPtr<ID3D11Texture2D> surface;

	CComPtr<IMFTransform> transform;
	CComPtr<IMFAttributes> attributes;
	CComPtr<ICodecAPI> spCodecApi;
	CComPtr<IMFAttributes> m_spEventGenerator;
	CComQIPtr<IMFMediaEventGenerator> eventGen;
	CComPtr<IMFTransform> spMFT ;

	DWORD inputStreamID;
	DWORD outputStreamID;

	long long ticksPerSecond;
	long long appStartTicks;
	long long encStartTicks;
	long long ticksPerFrame;
	GUID clsid;
	LPWSTR m_pszEncoderMFTName;

	// ------------------------------------------------------------------------
	// Initialize Media Foundation & COM
	// ------------------------------------------------------------------------

	hr = MFStartup(MF_VERSION);
	CHECK_HR(hr, "Failed to start Media Foundation");


	// ------------------------------------------------------------------------
	// Initialize D3D11
	// ------------------------------------------------------------------------

	// Driver types supported
	D3D_DRIVER_TYPE DriverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

	// Feature levels supported
	D3D_FEATURE_LEVEL FeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_1
	};
	UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

	D3D_FEATURE_LEVEL FeatureLevel;

	// Create device
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr,
			D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
			FeatureLevels, NumFeatureLevels, D3D11_SDK_VERSION, &device, &FeatureLevel, &context);
		if (SUCCEEDED(hr))
		{
			// Device creation success, no need to loop anymore
			break;
		}
	}

	CHECK_HR(hr, "Failed to create device");

	// Create device manager
	UINT resetToken;
	hr = MFCreateDXGIDeviceManager(&resetToken, &deviceManager);
	CHECK_HR(hr, "Failed to create DXGIDeviceManager");

	hr = deviceManager->ResetDevice(device, resetToken);
	CHECK_HR(hr, "Failed to assign D3D device to device manager");


	// ------------------------------------------------------------------------
	// Create surface
	// ------------------------------------------------------------------------

	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.Format = DXGI_FORMAT_NV12;
	desc.Width = surfaceWidth;
	desc.Height = surfaceHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;

	hr = device->CreateTexture2D(&desc, NULL, &surface);
	CHECK_HR(hr, "Could not create surface");


	// ------------------------------------------------------------------------
	// Initialize hardware encoder MFT
	// ------------------------------------------------------------------------

	// Find encoder
	CComHeapPtr<IMFActivate*> activateRaw;
	UINT32 activateCount = 0;


	UINT32 flags =
		MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_SYNCMFT |
		MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;
	flags |= MFT_ENUM_FLAG_HARDWARE;
	MFT_REGISTER_TYPE_INFO in;
	MFT_REGISTER_TYPE_INFO out;
	in.guidMajorType = MFMediaType_Video;
	in.guidSubtype = MFVideoFormat_NV12;
	out.guidMajorType = MFMediaType_Video;
	out.guidSubtype = MFVideoFormat_HEVC;

	hr = MFTEnumEx(
		MFT_CATEGORY_VIDEO_ENCODER,
		flags,
		NULL,
		&out,
		&activateRaw,
		&activateCount
	);
	CHECK_HR(hr, "Failed to enumerate MFTs");

	CHECK(activateCount, "No MFTs found");

	// Choose the first available encoder
	CComPtr<IMFActivate> activate = activateRaw[0]; // <<---------------- Index of MFT we're picking

	for (UINT32 i = 0; i < activateCount; i++)
		activateRaw[i]->Release();
	
	hr = activate->GetGUID(MFT_TRANSFORM_CLSID_Attribute, &clsid);
	
	hr = MFTGetInfo(clsid, &m_pszEncoderMFTName, NULL, NULL, NULL, NULL, NULL);
	hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IMFTransform, (void**)&spMFT);

	hr = activate->QueryInterface(IID_IMFMediaEventGenerator, (void**)&m_spEventGenerator);
	if (hr == E_NOINTERFACE)
	{
		std::wcout << "The selected MF encoder does not support async encode. Switching to sync mode." << std::endl;
	}

	static const GUID IID_ICodecAPI = {
	0x901db4c7, 0x31ce, 0x41a2, { 0x85, 0xdc, 0x8f, 0xa0, 0xbf, 0x41, 0xb8, 0xda }
	};
	hr = spMFT->QueryInterface(IID_ICodecAPI, (void**)&spCodecApi);
	CHECK_HR(hr, "QueryInterface( IID_ICodecApi ) failed");
	VARIANT var = { 0 };
	hr = spCodecApi->IsSupported(&CODECAPI_AVLowLatencyMode);
	if (hr == S_OK)
	{
		var.vt = VT_BOOL;
		var.boolVal = VARIANT_TRUE;
		hr = spCodecApi->SetValue(&CODECAPI_AVLowLatencyMode, &var);
	
	}


	CComHeapPtr<WCHAR> friendlyName;
	UINT32 friendlyNameLength;
	hr = activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendlyName, &friendlyNameLength);
	CHECK_HR(hr, "Failed to read MFT_FRIENDLY_NAME_Attribute");
	std::wcout << static_cast<WCHAR const*>(friendlyName) << std::endl;

	// Activate
	hr = activate->ActivateObject(IID_PPV_ARGS(&transform));
	CHECK_HR(hr, "Failed to activate MFT");

	// Get attributes
	hr = transform->GetAttributes(&attributes);
	CHECK_HR(hr, "Failed to get MFT attributes");

/*  NOTE: https://docs.microsoft.com/en-us/windows/win32/medfound/mft-friendly-name-attribute
	This attribute is supported by hardware-based MFTs. It is also set on the IMFActivate pointers... (see above)

	// Get encoder name
	UINT32 nameLength = 0;
	std::wstring name;

	hr = attributes->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &nameLength);
	CHECK_HR(hr, "Failed to get MFT name length");

	// IMFAttributes::GetString returns a null-terminated wide string
	name.resize(nameLength + 1);

	hr = attributes->GetString(MFT_FRIENDLY_NAME_Attribute, &name[0], name.size(), &nameLength);
	CHECK_HR(hr, "Failed to get MFT name");

	name.resize(nameLength);

	std::wcout << name << std::endl;
*/

	// Unlock the transform for async use and get event generator
	hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE); // <<---------------- Assuming asynchronous MFT without checking (fair, others are filtered out above)
	CHECK_HR(hr, "Failed to unlock MFT");

	eventGen = transform;
	CHECK(eventGen, "Failed to QI for event generator");

	// Get stream IDs (expect 1 input and 1 output stream)
	hr = transform->GetStreamIDs(1, &inputStreamID, 1, &outputStreamID);
	if (hr == E_NOTIMPL)
	{
		inputStreamID = 0;
		outputStreamID = 0;
		hr = S_OK;
	}
	CHECK_HR(hr, "Failed to get stream IDs");


	// ------------------------------------------------------------------------
	// Configure hardware encoder MFT
	// ------------------------------------------------------------------------

	// Set D3D manager
	hr = transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager.p)); // <<-------- Would fail if D3D11 device does not match in hardware to this MFT
	CHECK_HR(hr, "Failed to set D3D manager");

	// Set low latency hint
	hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
	CHECK_HR(hr, "Failed to set MF_LOW_LATENCY");

	// Set output type
	CComPtr<IMFMediaType> outputType;

	hr = MFCreateMediaType(&outputType);
	CHECK_HR(hr, "Failed to create media type");

	hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	CHECK_HR(hr, "Failed to set MF_MT_MAJOR_TYPE on H264 output media type");

	hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	CHECK_HR(hr, "Failed to set MF_MT_SUBTYPE on H264 output media type");

	hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, 30000000);
	CHECK_HR(hr, "Failed to set average bit rate on H264 output media type");

	hr = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, desc.Width, desc.Height);
	CHECK_HR(hr, "Failed to set frame size on H264 MFT out type");

	hr = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, 60, 1);
	CHECK_HR(hr, "Failed to set frame rate on H264 MFT out type");

	hr = outputType->SetUINT32(MF_MT_INTERLACE_MODE, 2);
	CHECK_HR(hr, "Failed to set MF_MT_INTERLACE_MODE on H.264 encoder MFT");

	hr = outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	CHECK_HR(hr, "Failed to set MF_MT_ALL_SAMPLES_INDEPENDENT on H.264 encoder MFT");

	hr = transform->SetOutputType(outputStreamID, outputType, 0);
	CHECK_HR(hr, "Failed to set output media type on H.264 encoder MFT");

	// Set input type
	CComPtr<IMFMediaType> inputType;

	hr = MFCreateMediaType(&inputType);
	CHECK_HR(hr, "Failed to create media type");

	for (DWORD i = 0;; i++)
	{
		inputType = nullptr;
		hr = transform->GetInputAvailableType(inputStreamID, i, &inputType);
		CHECK_HR(hr, "Failed to get input type");

		hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		CHECK_HR(hr, "Failed to set MF_MT_MAJOR_TYPE on H264 MFT input type");

		hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		CHECK_HR(hr, "Failed to set MF_MT_SUBTYPE on H264 MFT input type");

		hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, desc.Width, desc.Height);
		CHECK_HR(hr, "Failed to set MF_MT_FRAME_SIZE on H264 MFT input type");

		hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, 60, 1);
		CHECK_HR(hr, "Failed to set MF_MT_FRAME_RATE on H264 MFT input type");

		hr = transform->SetInputType(inputStreamID, inputType, 0);
		CHECK_HR(hr, "Failed to set input type");

		break;
	}


	// ------------------------------------------------------------------------
	// Start encoding
	// ------------------------------------------------------------------------

	// Initialize timer
	timeBeginPeriod(1);

	LARGE_INTEGER ticksInt;
	long long ticks;
	QueryPerformanceFrequency(&ticksInt);
	ticksPerSecond = ticksInt.QuadPart;

	QueryPerformanceCounter(&ticksInt);
	appStartTicks = ticksInt.QuadPart;

	ticksPerFrame = ticksPerSecond / frameRate;

	// Start encoder
	hr = transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
	CHECK_HR(hr, "Failed to process FLUSH command on H.264 MFT");

	hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
	CHECK_HR(hr, "Failed to process BEGIN_STREAMING command on H.264 MFT");

	hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
	CHECK_HR(hr, "Failed to process START_OF_STREAM command on H.264 MFT");


	// Main encode loop
	// Assume that METransformNeedInput and METransformHaveOutput are sent in a regular alternating pattern.
	// Otherwise a queue is needed for performance measurement.
	bool encoding = false;
	bool throttle = false;

	while (!(GetAsyncKeyState(quitKey) & (1 << 15)))
	{
		// Get next event
		CComPtr<IMFMediaEvent> event;
		hr = eventGen->GetEvent(0, &event);
		CHECK_HR(hr, "Failed to get next event");

		MediaEventType eventType;
		hr = event->GetType(&eventType);
		CHECK_HR(hr, "Failed to get event type");

		switch (eventType)
		{
		case METransformNeedInput:
			CHECK(!encoding, "Expected METransformHaveOutput");
			encoding = true;

			{
				throttle = !(GetAsyncKeyState(uncapKey) & (1 << 15));
				
				if (throttle)
				{
					// Calculate next frame time by quantizing time to the next value divisble by ticksPerFrame
					QueryPerformanceCounter(&ticksInt);
					ticks = ticksInt.QuadPart;

					long long nextFrameTicks = (ticks / ticksPerFrame + 1) * ticksPerFrame;

					// Wait for next frame
					while (ticks < nextFrameTicks)
					{
						// Not accurate, but enough for this purpose
						Sleep(1);

						QueryPerformanceCounter(&ticksInt);
						ticks = ticksInt.QuadPart;
					}
				}

				// Create buffer
				CComPtr<IMFMediaBuffer> inputBuffer;
				hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), surface, 0, FALSE, &inputBuffer);
				CHECK_HR(hr, "Failed to create IMFMediaBuffer");

				// Create sample
				CComPtr<IMFSample> sample;
				hr = MFCreateSample(&sample);
				CHECK_HR(hr, "Failed to create IMFSample");
				hr = sample->AddBuffer(inputBuffer);
				CHECK_HR(hr, "Failed to add buffer to IMFSample");

				// Start measuring encode time
				QueryPerformanceCounter(&ticksInt);
				encStartTicks = ticksInt.QuadPart;

				hr = transform->ProcessInput(inputStreamID, sample, 0);
				CHECK_HR(hr, "Failed to process input");
			}

			break;

		case METransformHaveOutput:
			CHECK(encoding, "Expected METransformNeedInput");
			encoding = false;

			{
				DWORD status;
				MFT_OUTPUT_DATA_BUFFER outputBuffer;
				outputBuffer.dwStreamID = outputStreamID;
				outputBuffer.pSample = nullptr;
				outputBuffer.dwStatus = 0;
				outputBuffer.pEvents = nullptr;

				hr = transform->ProcessOutput(0, 1, &outputBuffer, &status);
				CHECK_HR(hr, "ProcessOutput failed");

				// Stop measuring encode time
				QueryPerformanceCounter(&ticksInt);
				ticks = ticksInt.QuadPart;

				long double encTime_ms = (ticks - encStartTicks) * 1000 / (long double)ticksPerSecond;
				long double appTime_s = (encStartTicks - appStartTicks) / (long double)ticksPerSecond;

				// Report data
				std::cout << appTime_s << " " << encTime_ms << " " << throttle << std::endl;

				// Release sample as it is not processed any further.
				if (outputBuffer.pSample)
					outputBuffer.pSample->Release();
				if (outputBuffer.pEvents)
					outputBuffer.pEvents->Release();

				CHECK_HR(hr, "Failed to process output");
			}
			break;

		default:
			CHECK(false, "Unknown event");
			break;
		}
	}


	// ------------------------------------------------------------------------
	// Finish encoding
	// ------------------------------------------------------------------------

	hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL);
	CHECK_HR(hr, "Failed to process END_OF_STREAM command on H.264 MFT");

	hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL);
	CHECK_HR(hr, "Failed to process END_STREAMING command on H.264 MFT");

	hr = transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
	CHECK_HR(hr, "Failed to process FLUSH command on H.264 MFT");

	return 0;
}
