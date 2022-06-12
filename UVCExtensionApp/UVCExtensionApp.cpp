// UVCExtensionApp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <vector>
#include <string>
#include <ks.h>
#include <ksproxy.h>
#include <vidcap.h>
#include <ksmedia.h>
#include "UVCExtensionApp.h"

#define LED_CMD   0x01

static const GUID xuGuid =
  {0xE307E649, 0x4618, 0xA3FF, {0x82, 0xFC, 0x2D, 0x8B, 0x5F, 0x21, 0x67, 0x73}};

//Media foundation and DSHOW specific structures, class and variables
IMFMediaSource *pVideoSource = NULL;
IMFAttributes *pVideoConfig = NULL;
IMFActivate **ppVideoDevices = NULL;
IMFSourceReader *pVideoReader = NULL;

//Other variables
UINT32 noOfVideoDevices = 0;
WCHAR *szFriendlyName = NULL;

HRESULT FindExtensionNode(IKsTopologyInfo* pIksTopologyInfo, DWORD* pNodeId)
{
	DWORD numberOfNodes;
	HRESULT hResult = S_FALSE;

	hResult = pIksTopologyInfo->get_NumNodes(&numberOfNodes);
	if (SUCCEEDED(hResult)) {
		DWORD i;
		GUID nodeGuid;
		unsigned char *TmpGuid;

		for (i = 0; i < numberOfNodes; i++) {
			if (SUCCEEDED(pIksTopologyInfo->get_NodeType(i, &nodeGuid))) {
				// Found the extension node
				if (IsEqualGUID(KSNODETYPE_DEV_SPECIFIC, nodeGuid)) {
					*pNodeId = i;
					hResult = S_OK;
					break;
				}
			}
		}

		// Did not find the node 
		if (i == numberOfNodes) {
			hResult = S_FALSE;
		}
	}
	return hResult;
}

BOOL GetNodeId(int* pNodeId)
{
	IKsTopologyInfo* pKsToplogyInfo;
	HRESULT hResult;
	DWORD dwNode;

	hResult = pVideoSource->QueryInterface(__uuidof(IKsTopologyInfo), (void **)&pKsToplogyInfo);
	if (S_OK == hResult) {
		hResult = FindExtensionNode(pKsToplogyInfo, &dwNode);
		pKsToplogyInfo->Release();
		if (S_OK == hResult) {
			*pNodeId = dwNode;
			return TRUE;
		}
	}
	return FALSE;
}

int main()
{
	HRESULT hr = S_OK;
	CHAR enteredStr[MAX_PATH], videoDevName[20][MAX_PATH];
	UINT32 selectedVal = 0xFFFFFFFF;
	ULONG flags, readCount;
	size_t returnValue;
	BYTE data[10] = {0}; //Write some random value
	int NodeId;

	//Get all video devices
	GetVideoDevices();

	printf("Video Devices connected:\n");
	for (UINT32 i = 0; i < noOfVideoDevices; i++) {
		//Get the device names
		GetVideoDeviceFriendlyNames(i);
		wcstombs_s(&returnValue, videoDevName[i], MAX_PATH, szFriendlyName, MAX_PATH);
		printf("%d: %s\n", i, videoDevName[i]);

		//Store the App note firmware (Whose name is *FX3*) device index  
		if (!(strcmp(videoDevName[i], "UVC Camera")))
			selectedVal = i;
	}

	if (selectedVal != 0xFFFFFFFF) {
		printf("\nFound device\n");

		//Initialize the selected device
		InitVideoDevice(selectedVal);

		printf("\nSelect\n1. LED Set(ID 0x01) \n2. Led Get (ID 0x01)\nAnd press Enter\n");
		fgets(enteredStr, MAX_PATH, stdin);
		fflush(stdin);
		selectedVal = atoi(enteredStr);

		printf("\nTrying to invoke UVC extension unit...\n");
		GetNodeId(&NodeId);

		for (;;) {
			if (selectedVal == 1) {
				flags = KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY;
				if (!SetGetExtensionUnit(xuGuid, NodeId, LED_CMD, flags, (void*)data, 4, &readCount)) {
					printf("Found UVC extension unit\n");
					printf("\nSend Device LED command\r\n");
					break;
				} else {
					break;
				}
		
			} else {
				flags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_TOPOLOGY;
				if (!SetGetExtensionUnit(xuGuid, NodeId, LED_CMD, flags, (void*)data, 4, &readCount)) {
					printf("Found UVC extension unit\n");
					printf("\nGet Led %d.%d\n\n", data[0], data[1]);
					break;
				}
				else {
					break;
				}
		
			}
			Sleep(1000);
		}
	} else {
		printf("\nDid not find device \n");
	}

	//Release all the video device resources
	for (UINT32 i = 0; i < noOfVideoDevices; i++) {
		SafeRelease(&ppVideoDevices[i]);
	}
	CoTaskMemFree(ppVideoDevices);
	SafeRelease(&pVideoConfig);
	SafeRelease(&pVideoSource);
	CoTaskMemFree(szFriendlyName);

	printf("\nExiting App in 5 sec...");
	//Sleep(5000);

    return 0;
}

//Function to get UVC video devices
HRESULT GetVideoDevices()
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	MFStartup(MF_VERSION);

	// Create an attribute store to specify the enumeration parameters.
	HRESULT hr = MFCreateAttributes(&pVideoConfig, 1);
	CHECK_HR_RESULT(hr, "Create attribute store");

	// Source type: video capture devices
	hr = pVideoConfig->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
	);
	CHECK_HR_RESULT(hr, "Video capture device SetGUID");

	// Enumerate devices.
	hr = MFEnumDeviceSources(pVideoConfig, &ppVideoDevices, &noOfVideoDevices);
	CHECK_HR_RESULT(hr, "Device enumeration");

done:
	return hr;
}

//Function to get device friendly name
HRESULT GetVideoDeviceFriendlyNames(int deviceIndex)
{
	// Get the the device friendly name.
	UINT32 cchName;

	HRESULT hr = ppVideoDevices[deviceIndex]->GetAllocatedString(
		MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
		&szFriendlyName, &cchName);
	CHECK_HR_RESULT(hr, "Get video device friendly name");

done:
	return hr;
}

//Function to initialize video device
HRESULT InitVideoDevice(int deviceIndex)
{
	HRESULT hr = ppVideoDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(&pVideoSource));
	CHECK_HR_RESULT(hr, "Activating video device");

	// Create a source reader.
	hr = MFCreateSourceReaderFromMediaSource(pVideoSource, pVideoConfig, &pVideoReader);
	CHECK_HR_RESULT(hr, "Creating video source reader");

done:
	return hr;
}

//Function to set/get parameters of UVC extension unit
HRESULT SetGetExtensionUnit(GUID xuGuid, DWORD dwExtensionNode, ULONG xuPropertyId, ULONG flags, void *data, int len, ULONG *readCount)
{
	GUID pNodeType;
	IUnknown *unKnown;
	IKsControl * ks_control = NULL;
	IKsTopologyInfo * pKsTopologyInfo = NULL;
	KSP_NODE kspNode;

	HRESULT hr = pVideoSource->QueryInterface(__uuidof(IKsTopologyInfo), (void **)&pKsTopologyInfo);
	CHECK_HR_RESULT(hr, "IMFMediaSource::QueryInterface(IKsTopologyInfo)");

	hr = pKsTopologyInfo->get_NodeType(dwExtensionNode, &pNodeType);
	CHECK_HR_RESULT(hr, "IKsTopologyInfo->get_NodeType(...)");

	hr = pKsTopologyInfo->CreateNodeInstance(dwExtensionNode, IID_IUnknown, (LPVOID *)&unKnown);
	CHECK_HR_RESULT(hr, "ks_topology_info->CreateNodeInstance(...)");

	hr = unKnown->QueryInterface(__uuidof(IKsControl), (void **)&ks_control);
	CHECK_HR_RESULT(hr, "ks_topology_info->QueryInterface(...)");

	kspNode.Property.Set = xuGuid;					// XU GUID
	kspNode.NodeId = (ULONG)dwExtensionNode;		// XU Node ID
	kspNode.Property.Id = xuPropertyId;				// XU control ID
	kspNode.Property.Flags = flags;					// Set/Get request

	hr = ks_control->KsProperty((PKSPROPERTY)&kspNode, sizeof(kspNode), (PVOID)data, len, readCount);
	CHECK_HR_RESULT(hr, "ks_control->KsProperty(...)");

done:
	SafeRelease(&ks_control);
	return hr;
}