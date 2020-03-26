#include "OpenXR.hpp"

using namespace std;

OpenXR::~OpenXR() {

}

bool OpenXR::Init() {
	uint32_t tmp;

	uint32_t extensionCount;
	xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
	vector<XrExtensionProperties> extensions(extensionCount);
	if (extensionCount)
		xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &tmp, extensions.data());
	
	uint32_t apiLayerCount;
	xrEnumerateApiLayerProperties(0, &apiLayerCount, NULL);
	vector<XrApiLayerProperties> apiLayerProperties(apiLayerCount);
	if (apiLayerCount) {
		for (uint32_t i = 0; i < apiLayerCount; i++)
			apiLayerProperties[i].type = XR_TYPE_API_LAYER_PROPERTIES;
		xrEnumerateApiLayerProperties(apiLayerCount, &tmp, apiLayerProperties.data());
	}

	printf("OpenXR: Found %u extensions:\n", extensionCount);
	for (uint32_t i = 0; i < extensionCount; i++)
		printf("\t%s\n", extensions[i].extensionName);

	printf("OpenXR: Found %u API layers:\n", apiLayerCount);
	for (uint32_t i = 0; i < apiLayerCount; i++)
		printf("\t%s\n", apiLayerProperties[i].layerName);
	

	vector<const char*> enabledExtensions {
		
	};

	XrInstanceCreateInfo info = {};
	info.type = XR_TYPE_INSTANCE_CREATE_INFO;
	info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	memcpy(info.applicationInfo.engineName, "Stratum", strlen("Stratum"));
	info.applicationInfo.engineVersion = STRATUM_VERSION;
	memcpy(info.applicationInfo.applicationName, "Stratum", strlen("Stratum"));
	info.applicationInfo.applicationVersion = STRATUM_VERSION;
	info.enabledExtensionCount = enabledExtensions.size();
	info.enabledExtensionNames = enabledExtensions.data();
	info.enabledApiLayerCount = 0;
	if (XR_FAILED(xrCreateInstance(&info, &mInstance))) return false;

	return true;
}