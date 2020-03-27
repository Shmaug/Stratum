#include "OpenXR.hpp"
#include <Core/Device.hpp>
#include <Scene/Scene.hpp>
#include <Util/Tokenizer.hpp>

#include <openxr/openxr_platform.h>

using namespace std;

inline bool XR_FAILED_MSG(XrResult result, const string& errmsg) {
	if (XR_FAILED(result)) {
		fprintf_color(COLOR_RED, stderr, "%s: %u\n", errmsg.c_str(), (uint32_t)result);
		return true;
	}
	return false;
}

OpenXR::OpenXR() : mInstance(XR_NULL_HANDLE), mSession(XR_NULL_HANDLE), mScene(nullptr), mHmdCamera(nullptr) {}
OpenXR::~OpenXR() {
	if (mHmdCamera) mScene->RemoveObject(mHmdCamera);
	if (mSession) xrDestroySession(mSession);
	if (mInstance) xrDestroyInstance(mInstance);
}

bool OpenXR::Init() {
	uint32_t tmp;

	uint32_t extensionCount;
	xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
	vector<XrExtensionProperties> extensions(extensionCount);
	if (extensionCount) {
		for (uint32_t i = 0; i < extensionCount; i++)
			extensions[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &tmp, extensions.data());
		printf("Found %u OpenXR extensions:\n", extensionCount);
		for (uint32_t i = 0; i < extensionCount; i++)
			printf("\t%s\n", extensions[i].extensionName);
	} else printf("Found 0 OpenXR extensions.\n");

	uint32_t apiLayerCount;
	xrEnumerateApiLayerProperties(0, &apiLayerCount, NULL);
	vector<XrApiLayerProperties> apiLayerProperties(apiLayerCount);
	if (apiLayerCount) {
		for (uint32_t i = 0; i < apiLayerCount; i++)
			apiLayerProperties[i].type = XR_TYPE_API_LAYER_PROPERTIES;
		xrEnumerateApiLayerProperties(apiLayerCount, &tmp, apiLayerProperties.data());

		printf("Found %u OpenXR API layers:\n", apiLayerCount);
		for (uint32_t i = 0; i < apiLayerCount; i++)
			printf("\t%s\n", apiLayerProperties[i].layerName);
	} else printf("Found 0 OpenXR API layers.\n");
	
	vector<const char*> enabledExtensions { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };
	XrInstanceCreateInfo instanceinfo = {};
	instanceinfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
	instanceinfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	memcpy(instanceinfo.applicationInfo.engineName, "Stratum", strlen("Stratum"));
	instanceinfo.applicationInfo.engineVersion = STRATUM_VERSION;
	memcpy(instanceinfo.applicationInfo.applicationName, "Stratum", strlen("Stratum"));
	instanceinfo.applicationInfo.applicationVersion = STRATUM_VERSION;
	instanceinfo.enabledExtensionCount = enabledExtensions.size();
	instanceinfo.enabledExtensionNames = enabledExtensions.data();
	instanceinfo.enabledApiLayerCount = 0;
	if (XR_FAILED_MSG(xrCreateInstance(&instanceinfo, &mInstance), "xrCreateInstance failed")) return false;
	XrInstanceProperties instanceProperties = {};
	instanceProperties.type = XR_TYPE_INSTANCE_PROPERTIES;
	if (XR_FAILED_MSG(xrGetInstanceProperties(mInstance, &instanceProperties), "xrGetInstanceProperties failed")) return false;
	printf_color(COLOR_GREEN, "OpenXR Instance created: %s.\n", instanceProperties.runtimeName);


	XrSystemGetInfo systeminfo = {};
	systeminfo.type = XR_TYPE_SYSTEM_GET_INFO;
	systeminfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (XR_FAILED(xrGetSystem(mInstance, &systeminfo, &mSystem))) return false;
	mSystemProperties = {};
	mSystemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;
	if (XR_FAILED_MSG(xrGetSystemProperties(mInstance, mSystem, &mSystemProperties), "xrGetSystemProperties failed")) return false;
	printf_color(COLOR_GREEN, "OpenXR system found: %s.\n", mSystemProperties.systemName);

	return true;
}

void OpenXR::PreInstanceInit(Instance* instance) {
	PFN_xrVoidFunction func;
	if (XR_FAILED_MSG(xrGetInstanceProcAddr(mInstance, "xrGetVulkanInstanceExtensionsKHR", &func), "Failed to get address of xrGetVulkanInstanceExtensionsKHR")) return;
	PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensions = (PFN_xrGetVulkanInstanceExtensionsKHR)func;

	uint32_t bufSize, len;
	xrGetVulkanInstanceExtensions(mInstance, mSystem, 0, &bufSize, nullptr);
	string extensions(bufSize, '\0');
	xrGetVulkanInstanceExtensions(mInstance, mSystem, bufSize, &len, extensions.data());

	Tokenizer t(extensions, { ' ' });
	string e;
	while (t.Next(e)) instance->RequestInstanceExtension(e);
}
void OpenXR::PreDeviceInit(Instance* instance, VkPhysicalDevice device) {
	PFN_xrVoidFunction func;
	if (XR_FAILED_MSG(xrGetInstanceProcAddr(mInstance, "xrGetVulkanDeviceExtensionsKHR", &func), "Failed to get address of xrGetVulkanDeviceExtensionsKHR")) return;
	PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensions = (PFN_xrGetVulkanDeviceExtensionsKHR)func;

	uint32_t bufSize, len;
	xrGetVulkanDeviceExtensions(mInstance, mSystem, 0, &bufSize, nullptr);
	string extensions(bufSize, '\0');
	xrGetVulkanDeviceExtensions(mInstance, mSystem, bufSize, &len, extensions.data());

	Tokenizer t(extensions, { ' ' });
	string e;
	while (t.Next(e)) instance->RequestDeviceExtension(e);
}

bool OpenXR::InitScene(Scene* scene) {
	mScene = scene;

	uint32_t viewConfigurationCount, tmp;
	xrEnumerateViewConfigurations(mInstance, mSystem, 0, &viewConfigurationCount, nullptr);
	vector<XrViewConfigurationType> viewConfigurations(viewConfigurationCount);
	xrEnumerateViewConfigurations(mInstance, mSystem, viewConfigurationCount, &tmp, viewConfigurations.data());
	mViewConfiguration = viewConfigurations[0];
	for (uint32_t i = 0; i < viewConfigurationCount; i++)
		if (viewConfigurations[i] == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
			mViewConfiguration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			break;
		}

	uint32_t viewCount;
	xrEnumerateViewConfigurationViews(mInstance, mSystem, mViewConfiguration, 0, &viewCount, nullptr);
	vector<XrViewConfigurationView> views(viewCount);
	for (uint32_t i = 0; i < viewCount; i++) views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	xrEnumerateViewConfigurationViews(mInstance, mSystem, mViewConfiguration, viewCount, &tmp, views.data());
	
	// View configuration should have just two views (eyes) that are identical...
	if (viewCount != 2 ||
		views[0].recommendedSwapchainSampleCount != views[1].recommendedSwapchainSampleCount ||
		views[0].recommendedImageRectWidth != views[1].recommendedImageRectWidth || views[0].recommendedImageRectHeight != views[1].recommendedImageRectHeight) {
		fprintf_color(COLOR_RED, stderr, "Unsupported OpenXR view configuration.\n");
		return false;
	}

	XrGraphicsBindingVulkanKHR binding = {};
	binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
	binding.instance = *mScene->Instance();
	binding.physicalDevice = mScene->Instance()->Device()->PhysicalDevice();
	binding.device = *mScene->Instance()->Device();
	binding.queueFamilyIndex = mScene->Instance()->Device()->PresentQueueFamilyIndex();
	binding.queueIndex = mScene->Instance()->Device()->PresentQueueIndex();

	XrSessionCreateInfo sessioninfo = {};
	sessioninfo.type = XR_TYPE_SESSION_CREATE_INFO;
	sessioninfo.systemId = mSystem;
	sessioninfo.next = &binding;
	if (XR_FAILED_MSG(xrCreateSession(mInstance, &sessioninfo, &mSession), "xrCreateSession failed")) return false;
	printf_color(COLOR_GREEN, "%s", "OpenXR Session created.\n");

	VkSampleCountFlags sampleCount = VK_SAMPLE_COUNT_1_BIT;
	switch (views[0].recommendedSwapchainSampleCount) {
		case 1:
			sampleCount = VK_SAMPLE_COUNT_1_BIT;
			break;
		case 2:
			sampleCount = VK_SAMPLE_COUNT_2_BIT;
			break;
		case 4:
			sampleCount = VK_SAMPLE_COUNT_2_BIT;
			break;
		case 8:
			sampleCount = VK_SAMPLE_COUNT_8_BIT;
			break;
		case 16:
			sampleCount = VK_SAMPLE_COUNT_16_BIT;
			break;
		case 32:
			sampleCount = VK_SAMPLE_COUNT_32_BIT;
			break;
		case 64:
			sampleCount = VK_SAMPLE_COUNT_64_BIT;
			break;
		default:
			fprintf_color(COLOR_YELLOW, stderr, "Unsupported OpenXR recommended sample count: %u.\n", views[0].recommendedSwapchainSampleCount);
			break;
	}

	auto camera = make_shared<Camera>(mSystemProperties.systemName, mScene->Instance()->Device(), VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, sampleCount);
	camera->FramebufferWidth(views[0].recommendedImageRectWidth);
	camera->FramebufferHeight(views[0].recommendedImageRectHeight * 2);
	mScene->AddObject(camera);
	mHmdCamera = camera.get();


	// TODO: Create action sets, reference spaces, action spaces, and swapchain
	
	return true;
}

void OpenXR::PollEvents() {
	XrEventDataBuffer event = {};
	event.type = XR_TYPE_EVENT_DATA_BUFFER;
	XrResult result;
	if (XR_FAILED_MSG(result = xrPollEvent(mInstance, &event), "xrPollEvent failed")) return;

	if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
		printf_color(COLOR_YELLOW, "%s", "xrInstance loss pending\n");
		return;
	}
	if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
		printf_color(COLOR_YELLOW, "%s", "xrSession state changed\n");
		return;
	}

	XrActionsSyncInfo sync = {};
	sync.type = XR_TYPE_ACTIONS_SYNC_INFO;
	if (XR_FAILED_MSG(xrSyncActions(mSession, &sync), "xrSyncActions failed")) return;

}