#include "XR.hpp"


using namespace stm;

bool XR::FailMsg(XrResult result, const string& errmsg) {
	if (XR_FAILED(result)) {
		char resultString[XR_MAX_RESULT_STRING_SIZE];
		xrResultToString(mInstance, result, resultString);
		fprintf_color(ConsoleColorBits::eRed, stderr, "%s: %s\n", errmsg.c_str(), resultString);
		return true;
	}
	return false;
}

XR::XR() : mInstance(XR_NULL_HANDLE), mSession(XR_NULL_HANDLE), mReferenceSpace(XR_NULL_HANDLE), mActionSet(XR_NULL_HANDLE),
	mGrabAction(XR_NULL_HANDLE), mPoseAction(XR_NULL_HANDLE), mScene(nullptr), mHmdCamera(nullptr) {

	uint32_t tmp;
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
	strcpy(instanceinfo.applicationInfo.engineName, "Stratum");
	instanceinfo.applicationInfo.engineVersion = VK_MAKE_VERSION(STRATUM_VERSION_MAJOR,STRATUM_VERSION_MINOR,0);
	strcpy(instanceinfo.applicationInfo.applicationName, "Stratum");
	instanceinfo.applicationInfo.applicationVersion = VK_MAKE_VERSION(0,0,0);
	instanceinfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
	instanceinfo.enabledExtensionNames = enabledExtensions.data();
	instanceinfo.enabledApiLayerCount = 0;
	if (FailMsg(xrCreateInstance(&instanceinfo, &mInstance), "xrCreateInstance failed")) {
		mInitialized = false;
		return;
	}
	XrInstanceProperties instanceProperties = {};
	instanceProperties.type = XR_TYPE_INSTANCE_PROPERTIES;
	if (FailMsg(xrGetInstanceProperties(mInstance, &instanceProperties), "xrGetInstanceProperties failed")) {
		Cleanup();
		mInitialized = false;
		return;
	}
	printf_color(ConsoleColorBits::eGreen, "OpenXR Instance created: %s.\n", instanceProperties.runtimeName);

	XrSystemGetInfo systeminfo = {};
	systeminfo.type = XR_TYPE_SYSTEM_GET_INFO;
	systeminfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (XR_FAILED(xrGetSystem(mInstance, &systeminfo, &mSystem))) {
		mInitialized = false;
		return;
	}
	mSystemProperties = {};
	mSystemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;
	if (FailMsg(xrGetSystemProperties(mInstance, mSystem, &mSystemProperties), "xrGetSystemProperties failed")) {
		Cleanup();
		mInitialized = false;
		return;
	}
	printf_color(ConsoleColorBits::eGreen, "OpenXR system found: %s.\n", mSystemProperties.systemName);
}
XR::~XR() { Cleanup(); }

void XR::Cleanup() {
	safe_delete(mHmdCamera);

	for (uint32_t i = 0; i < mSwapchainImages.size(); i++) delete[] mSwapchainImages[i];
	for (uint32_t i = 0; i < mSwapchains.size(); i++) xrDestroySwapchain(mSwapchains[i]);

	if (mPoseAction) xrDestroyAction(mPoseAction);
	if (mGrabAction) xrDestroyAction(mGrabAction);

	if (mActionSet) xrDestroyActionSet(mActionSet);
	for (uint32_t i = 0; i < mActionSpaces.size(); i++) xrDestroySpace(mActionSpaces[i]);
	if (mReferenceSpace) xrDestroySpace(mReferenceSpace); 

	if (mSession){
		xrEndSession(mSession);
		xrDestroySession(mSession);
	}

	if (mInstance) xrDestroyInstance(mInstance);

	mSwapchainImages = {};
	mSwapchains = {};
	mActionSpaces = {};
	mInstance = XR_NULL_HANDLE;
	mSession = XR_NULL_HANDLE;
	mReferenceSpace = XR_NULL_HANDLE;
	mActionSet = XR_NULL_HANDLE;
	mPoseAction = XR_NULL_HANDLE;
	mGrabAction = XR_NULL_HANDLE;
	mHmdCamera = nullptr;
}

unordered_set<string> XR::InstanceExtensionsRequired() {
	PFN_xrVoidFunction func;
	if (FailMsg(xrGetInstanceProcAddr(mInstance, "xrGetVulkanInstanceExtensionsKHR", &func), "Failed to get address of xrGetVulkanInstanceExtensionsKHR")) return {};
	PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensions = (PFN_xrGetVulkanInstanceExtensionsKHR)func;

	uint32_t bufSize, len;
	xrGetVulkanInstanceExtensions(mInstance, mSystem, 0, &bufSize, nullptr);
	string extensions(bufSize, '\0');
	xrGetVulkanInstanceExtensions(mInstance, mSystem, bufSize, &len, extensions.data());

	unordered_set<string> result;
	stringstream t(extensions);
	string e;
	while (t >> e) result.insert(e);
	return result;
}
unordered_set<string> XR::DeviceExtensionsRequired(vk::PhysicalDevice device) {
	PFN_xrVoidFunction func;
	if (FailMsg(xrGetInstanceProcAddr(mInstance, "xrGetVulkanDeviceExtensionsKHR", &func), "Failed to get address of xrGetVulkanDeviceExtensionsKHR")) return {};
	PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensions = (PFN_xrGetVulkanDeviceExtensionsKHR)func;

	uint32_t bufSize, len;
	xrGetVulkanDeviceExtensions(mInstance, mSystem, 0, &bufSize, nullptr);
	string extensions(bufSize, '\0');
	xrGetVulkanDeviceExtensions(mInstance, mSystem, bufSize, &len, extensions.data());

	unordered_set<string> result;
	stringstream t(extensions);
	string e;
	while (t >> e) result.insert(e);
	return result;
}

bool XR::Init(Scene* scene) {
	if (!mInitialized) return false;
	
	mScene = scene;

	#pragma region View configuration enumeration
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

	xrEnumerateViewConfigurationViews(mInstance, mSystem, mViewConfiguration, 0, &mViewCount, nullptr);
	vector<XrViewConfigurationView> views(mViewCount);
	for (uint32_t i = 0; i < mViewCount; i++) views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	xrEnumerateViewConfigurationViews(mInstance, mSystem, mViewConfiguration, mViewCount, &tmp, views.data());
	
	// View configuration should have just two views (eyes) that are identical...
	if (mViewCount != 2 ||
		views[0].recommendedSwapchainSampleCount != views[1].recommendedSwapchainSampleCount ||
		views[0].recommendedImageRectWidth != views[1].recommendedImageRectWidth || views[0].recommendedImageRectHeight != views[1].recommendedImageRectHeight) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "%s", "Unsupported OpenXR view configuration.\n");
		Cleanup();
		return false;
	}

	uint32_t sampleCountInt = 1;
	vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
	switch (views[0].recommendedSwapchainSampleCount) {
		case 1:
			sampleCount = vk::SampleCountFlagBits::e1;
			sampleCountInt = 1;
			break;
		case 2:
			sampleCount = vk::SampleCountFlagBits::e2;
			sampleCountInt = 2;
			break;
		case 4:
			sampleCount = vk::SampleCountFlagBits::e4;
			sampleCountInt = 4;
			break;
		case 8:
			sampleCount = vk::SampleCountFlagBits::e8;
			sampleCountInt = 8;
			break;
		case 16:
		case 32:
		case 64:
			sampleCount = vk::SampleCountFlagBits::e16;
			sampleCountInt = 16;
			break;
		default:
			fprintf_color(ConsoleColorBits::eYellow, stderr, "Unsupported OpenXR recommended sample count: %u.\n", views[0].recommendedSwapchainSampleCount);
			break;
	}
	#pragma endregion

	CreateSession();
	if (!mSession) return false;

	#pragma region Swapchain creation
	uint32_t formatCount;
	xrEnumerateSwapchainFormats(mSession, 0, &formatCount, nullptr);
	vector<int64_t> formats(formatCount);
	xrEnumerateSwapchainFormats(mSession, formatCount, &tmp, formats.data());

	mSwapchainFormat = (vk::Format)formats[0];
	for (uint32_t i = 0; i < formatCount; i++)
		if ((vk::Format)formats[i] == vk::Format::eR8G8B8A8Unorm) {
			mSwapchainFormat = (vk::Format)formats[i];
			break;
		} else if ((vk::Format)formats[i] == vk::Format::eB8G8R8A8Unorm)
			mSwapchainFormat = (vk::Format)formats[i];
	

	uint32_t maxSwapchainLength = 0;

	mSwapchains.resize(mViewCount);
	mSwapchainImages.resize(mViewCount);

	for (uint32_t i = 0; i < mViewCount; i++) {
		XrSwapchainCreateInfo info = {};
		info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
		info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		info.width = views[i].recommendedImageRectWidth;
		info.height = views[i].recommendedImageRectHeight;
		info.format = (VkFormat)mSwapchainFormat;
		info.sampleCount = 1;
		info.faceCount = 1;
		info.arraySize = 1;
		info.mipCount = 1;
		if (FailMsg(xrCreateSwapchain(mSession, &info, &mSwapchains[i]), "xrCreateSwapchain failed")) {
			Cleanup();
			return false;
		}

		uint32_t len;
		if (FailMsg(xrEnumerateSwapchainImages(mSwapchains[i], 0, &len, nullptr), "xrEnumerateSwapchainImages failed")) {
			Cleanup();
			return false;
		}	               

		maxSwapchainLength = max(maxSwapchainLength, len);
	}

	for (uint32_t i = 0; i < mViewCount; i++) {
		mSwapchainImages[i] = new XrSwapchainImageVulkanKHR[maxSwapchainLength];
		for (uint32_t j = 0; j < maxSwapchainLength; j++)
			mSwapchainImages[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
		
		uint32_t len;
		if (FailMsg(xrEnumerateSwapchainImages(mSwapchains[i], maxSwapchainLength, &len, (XrSwapchainImageBaseHeader*)mSwapchainImages[i]), "xrEnumerateSwapchainImages failed")) {
			Cleanup();
			return false;
		}	         
	}

	mProjectionViews.resize(mViewCount);
	for (uint32_t i = 0; i < mViewCount; i++) {
		mProjectionViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		mProjectionViews[i].subImage.swapchain = mSwapchains[i];
		mProjectionViews[i].subImage.imageRect.offset.x = 0;
		mProjectionViews[i].subImage.imageRect.offset.y = 0;
		mProjectionViews[i].subImage.imageRect.extent.width = views[i].recommendedImageRectWidth;
		mProjectionViews[i].subImage.imageRect.extent.height = views[i].recommendedImageRectHeight;
	}
	#pragma endregion
	
	#pragma region Action creation
	XrActionSetCreateInfo setInfo = {};
	setInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	strcpy(setInfo.actionSetName, "actionset");
	strcpy(setInfo.localizedActionSetName, "Action Set");
	if (FailMsg(xrCreateActionSet(mInstance, &setInfo, &mActionSet), "xrCreateActionSet failed")) {
		Cleanup();
		return false;
	}

	mActionSpaces.resize(2); // 2 hands
	mHandPaths.resize(2);
	XrPath selectClickPath[2];
	XrPath posePath[2];

	xrStringToPath(mInstance, "/user/hand/left", &mHandPaths[0]);
	xrStringToPath(mInstance, "/user/hand/right", &mHandPaths[1]);

	XrActionCreateInfo actionInfo = {};
	actionInfo.type = XR_TYPE_ACTION_CREATE_INFO;
	actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
	actionInfo.countSubactionPaths = (uint32_t)mHandPaths.size();
	actionInfo.subactionPaths = mHandPaths.data();
	strcpy(actionInfo.actionName, "triggergrab"); // assuming every controller has some form of main "trigger" button
	strcpy(actionInfo.localizedActionName, "Grab Object with Trigger Button");
	if (FailMsg(xrCreateAction(mActionSet, &actionInfo, &mGrabAction), "xrCreateAction failed for triggergrab")) {
		Cleanup();
		return false;
	}

	actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
	strcpy(actionInfo.actionName, "handpose");
	strcpy(actionInfo.localizedActionName, "Hand Pose");
	actionInfo.countSubactionPaths = (uint32_t)mHandPaths.size();
	actionInfo.subactionPaths = mHandPaths.data();
	if (FailMsg(xrCreateAction(mActionSet, &actionInfo, &mPoseAction), "xrCreateAction failed for handpose")) {
		Cleanup();
		return false;
	}

	xrStringToPath(mInstance, "/user/hand/left/input/select/click", &selectClickPath[0]);
	xrStringToPath(mInstance, "/user/hand/right/input/select/click", &selectClickPath[1]);
	xrStringToPath(mInstance, "/user/hand/left/input/grip/pose", &posePath[0]);
	xrStringToPath(mInstance, "/user/hand/right/input/grip/pose", &posePath[1]);

	XrPath khrSimpleInteractionProfilePath;
	if (FailMsg(xrStringToPath(mInstance, "/interaction_profiles/khr/simple_controller", &khrSimpleInteractionProfilePath), "xrStringToPath failed for /interaction_profiles/khr/simple_controller")) {
		Cleanup();
		return false;
	}

	vector<XrActionSuggestedBinding> bindings(4);
	bindings[0].action = mPoseAction;
	bindings[0].binding = posePath[0];
	bindings[1].action = mPoseAction;
	bindings[1].binding = posePath[1];
	bindings[2].action = mGrabAction;
	bindings[2].binding = selectClickPath[0];
	bindings[3].action = mGrabAction;
	bindings[3].binding = selectClickPath[1];

	XrInteractionProfileSuggestedBinding suggestedBindings = {};
	suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
	suggestedBindings.interactionProfile = khrSimpleInteractionProfilePath;
	suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
	suggestedBindings.suggestedBindings = bindings.data();
	if (FailMsg(xrSuggestInteractionProfileBindings(mInstance, &suggestedBindings), "xrSuggestInteractionProfileBindings failed")) {
		Cleanup();
		return false;
	}

	// Create action space for each hand
	XrActionSpaceCreateInfo actionSpaceInfo = {};
	actionSpaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
	actionSpaceInfo.action = mPoseAction;
	actionSpaceInfo.poseInActionSpace.position = { 0.f, 0.f, 0.f };
	actionSpaceInfo.poseInActionSpace.orientation = { 0.f, 0.f, 0.f, 1.f };
	for (uint32_t i = 0; i < mActionSpaces.size(); i++){
		actionSpaceInfo.subactionPath = mHandPaths[i];
		if (FailMsg(xrCreateActionSpace(mSession, &actionSpaceInfo, &mActionSpaces[i]), "xrCreateActionSpace failed")) {
			Cleanup();
			return false;
		}
	}

	XrSessionActionSetsAttachInfo attachInfo = {};
	attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &mActionSet;
	if (FailMsg(xrAttachSessionActionSets(mSession, &attachInfo), "xrAttachSessionActionSets failed")) {
		Cleanup();
		return false;
	}
	#pragma endregion

	mHmdCamera = mScene->CreateObject<Camera>(mSystemProperties.systemName, unordered_set<RenderAttachmentId> { "OpenXR HMD" });
	mHmdCamera->StereoMode(StereoMode::eHorizontal);

	return true;
}

void XR::CreateSession() {
	XrGraphicsBindingVulkanKHR binding = {};
	binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
	binding.instance = *mScene->mInstance;
	binding.physicalDevice = mScene->mInstance.Device().PhysicalDevice();
	binding.device = *mScene->mInstance.Device();
	binding.queueFamilyIndex = mScene->mInstance.Window().PresentQueueFamily()->mFamilyIndex;
	binding.queueIndex = 0;

	XrSessionCreateInfo sessioninfo = {};
	sessioninfo.type = XR_TYPE_SESSION_CREATE_INFO;
	sessioninfo.systemId = mSystem;
	sessioninfo.next = &binding;
	if (FailMsg(xrCreateSession(mInstance, &sessioninfo, &mSession), "xrCreateSession failed")) return;
	printf_color(ConsoleColorBits::eGreen, "%s", "OpenXR Session created.\n");


	uint32_t spaceCount, tmp;
	xrEnumerateReferenceSpaces(mSession, 0, &spaceCount, nullptr);
	vector<XrReferenceSpaceType> referenceSpaces(spaceCount);
	xrEnumerateReferenceSpaces(mSession, spaceCount, &tmp, referenceSpaces.data());

	// prefer Stage, then Local, then View
	mReferenceSpaceType = referenceSpaces[0];
	for (uint32_t i = 0; i < spaceCount; i++)
		if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
			mReferenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
			break;
		} else if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_LOCAL)
			mReferenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	
	XrReferenceSpaceCreateInfo referenceSpaceInfo = {};
	referenceSpaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	referenceSpaceInfo.referenceSpaceType = mReferenceSpaceType;
	referenceSpaceInfo.poseInReferenceSpace.orientation = { 0.f, 0.f, 0.f, 1.f};
	referenceSpaceInfo.poseInReferenceSpace.position = { 0.f, 0.f, 0.f };
	if (FailMsg(xrCreateReferenceSpace(mSession, &referenceSpaceInfo, &mReferenceSpace), "xrCreateReferenceSpace failed")) {
		xrDestroySession(mSession);
		mSession = XR_NULL_HANDLE;
		return;
	}

	if (mReferenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE){
		XrExtent2Df bounds;
		if (XR_SUCCEEDED(xrGetReferenceSpaceBoundsRect(mSession, mReferenceSpaceType, &bounds)))
			printf("XR_REFERENCE_SPACE_TYPE_STAGE: %.3fx%.3f\n", bounds.width, bounds.height);
	}

	XrSessionBeginInfo begin = {};
	begin.type = XR_TYPE_SESSION_BEGIN_INFO;
	begin.primaryViewConfigurationType = mViewConfiguration;
	if (FailMsg(xrBeginSession(mSession, &begin), "xrBeginSession failed")) {
		xrDestroySpace(mReferenceSpace);
		xrDestroySession(mSession);
		mSession = XR_NULL_HANDLE;
		return;
	}
}

void XR::OnFrameStart() {
	if (!mInstance || !mSession) return;

	mFrameState = {};
	mFrameState.type = XR_TYPE_FRAME_STATE;

	XrFrameWaitInfo frameWaitInfo = {};
	frameWaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
	if (FailMsg(xrWaitFrame(mSession, &frameWaitInfo, &mFrameState), "xrWaitFrame failed")) {
		Cleanup();
		return;
	}

	#pragma region Poll events
	XrEventDataBuffer event = {};
	event.type = XR_TYPE_EVENT_DATA_BUFFER;
	if (FailMsg(xrPollEvent(mInstance, &event), "xrPollEvent failed")) return;

	switch (event.type) {
	case XR_TYPE_EVENT_DATA_EVENTS_LOST:
		break;

	case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:{
		XrEventDataSessionStateChanged* state = (XrEventDataSessionStateChanged*)&event;
		switch (state->state) {
    	case XR_SESSION_STATE_UNKNOWN:
    	case XR_SESSION_STATE_IDLE:
    	case XR_SESSION_STATE_READY:
    	case XR_SESSION_STATE_SYNCHRONIZED:
    	case XR_SESSION_STATE_VISIBLE:
    	case XR_SESSION_STATE_FOCUSED:
			break;

    	case XR_SESSION_STATE_STOPPING:
    	case XR_SESSION_STATE_EXITING:
		case XR_SESSION_STATE_LOSS_PENDING:
			printf_color(ConsoleColorBits::eYellow, "%s", "xrSession state lost\n");
			Cleanup();
			return;
		}
		break;
	}
	case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
		printf_color(ConsoleColorBits::eYellow, "%s", "xrInstance lost\n");
		Cleanup();
		return;
	}
	#pragma endregion

	XrFrameBeginInfo frameBeginInfo = {};
	frameBeginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
	if (FailMsg(xrBeginFrame(mSession, &frameBeginInfo), "xrBeginFrame failed")) {
		Cleanup();
		return;
	}

	#pragma region Sync actions
	XrActiveActionSet activeActionSet = {};
	activeActionSet.actionSet = mActionSet;

	XrActionsSyncInfo syncInfo = {};
	syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeActionSet;
	FailMsg(xrSyncActions(mSession, &syncInfo), "xrSyncActions failed");

	XrActionStateGetInfo getInfo = {};
	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.action = mGrabAction;
	getInfo.subactionPath = mHandPaths[0];

	XrActionStateFloat grabValue = {};
	grabValue.type = XR_TYPE_ACTION_STATE_FLOAT;
	FailMsg(xrGetActionStateFloat(mSession, &getInfo, &grabValue), "xrGetActionStateFloat failed for Grab Action");

	getInfo.action = mPoseAction;
	XrActionStatePose poseState = {};
	poseState.type = XR_TYPE_ACTION_STATE_POSE;
	FailMsg(xrGetActionStatePose(mSession, &getInfo, &poseState), "xrGetActionStatePose failed for Pose Action");

	vector<XrSpaceLocation> spaceLocation(mActionSpaces.size());
	for (uint32_t i = 0; i < mActionSpaces.size(); i++) {
		spaceLocation[i].type = XR_TYPE_SPACE_LOCATION;
		FailMsg(xrLocateSpace(mActionSpaces[i], mReferenceSpace, mFrameState.predictedDisplayTime, &spaceLocation[i]), "xrLocateSpace failed");
	}
	#pragma endregion

	// Advance swapchains
	for (uint32_t i = 0; i < mViewCount; i++){
		XrSwapchainImageAcquireInfo swapchainImageAcquireInfo = {};
		swapchainImageAcquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
		uint32_t bufferIndex;
		if (FailMsg(xrAcquireSwapchainImage(mSwapchains[i], &swapchainImageAcquireInfo, &bufferIndex), "xrAcquireSwapchainImage failed")) {
			Cleanup();
			return;
		}

		XrSwapchainImageWaitInfo swapchainImageWaitInfo = {};
		swapchainImageWaitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
		swapchainImageWaitInfo.timeout = 1000;
		if (FailMsg(xrWaitSwapchainImage(mSwapchains[i], &swapchainImageWaitInfo), "xrWaitSwapchainImage failed")) {
			Cleanup();
			return;
		}

		mProjectionViews[i].subImage.imageArrayIndex = bufferIndex;
	}
}

void XR::PostRender(CommandBuffer& commandBuffer) {
	#pragma region Locate views (eyes) within mReferenceSpace
	uint32_t tmp;
	vector<XrView> views(mViewCount);
	for (uint32_t i = 0; i < mViewCount; i++) views[i].type = XR_TYPE_VIEW;

	XrViewLocateInfo viewLocateInfo = {};
	viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	viewLocateInfo.displayTime = mFrameState.predictedDisplayTime;
	viewLocateInfo.space = mReferenceSpace;
	XrViewState viewState = {};
	viewState.type = XR_TYPE_VIEW_STATE;
	if (FailMsg(xrLocateViews(mSession, &viewLocateInfo, &viewState, mViewCount, &tmp, views.data()), "xrLocateViews failed")) {
		Cleanup();
		return;
	}
	#pragma endregion

	vector<Vector3f> positions(mViewCount);
	vector<fquat> rotations(mViewCount);

	StereoEye eyes[2] { StereoEye::eLeft, StereoEye::eRight };

	float n = mHmdCamera->Near();

	// Update projection views
	for (uint32_t i = 0; i < mViewCount; i++) {
		mProjectionViews[i].pose = views[i].pose;
		mProjectionViews[i].fov = views[i].fov;

		mHmdCamera->Projection(Matrix4f::Perspective(
			n * tanf(views[i].fov.angleLeft), n * tanf(views[i].fov.angleRight), n * tanf(views[i].fov.angleDown), n * tanf(views[i].fov.angleUp),
			n, mHmdCamera->Far()), eyes[i]);
			
		positions[i] = Vector3f(views[i].pose.position.x, views[i].pose.position.y, -views[i].pose.position.z);
		rotations[i] = fquat(-views[i].pose.orientation.x, -views[i].pose.orientation.y, views[i].pose.orientation.z, views[i].pose.orientation.w);
	}

	Vector3f center = (positions[0] + positions[1]) * .5f;
	fquat iq = inverse(rotations[0]);

	mHmdCamera->LocalPosition(center);
	mHmdCamera->LocalRotation(rotations[0]);

	mHmdCamera->EyeOffset(iq * (positions[0] - center), fquat::Identity(), eyes[0]);
	mHmdCamera->EyeOffset(iq * (positions[1] - center), iq * rotations[1], eyes[1]);


	// TODO: Copy render result into right and left respectively
	//mHmdCamera->WriteUniformBuffer();
	vk::Image right = mSwapchainImages[0][mProjectionViews[0].subImage.imageArrayIndex].image;
	vk::Image left = mSwapchainImages[1][mProjectionViews[1].subImage.imageArrayIndex].image;
}

void XR::OnFrameEnd() {
	for (uint32_t i = 0; i < mViewCount; i++) {
		XrSwapchainImageReleaseInfo swapchainImageReleaseInfo = {};
		swapchainImageReleaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
		if (FailMsg(xrReleaseSwapchainImage(mSwapchains[i], &swapchainImageReleaseInfo), "xrReleaseSwapchainImage failed")) {
			Cleanup();
			return;
		}
	}

	XrCompositionLayerProjection projectionLayer = {};
	projectionLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
	projectionLayer.space = mReferenceSpace;
	projectionLayer.viewCount = mViewCount;
	projectionLayer.views = mProjectionViews.data();

	XrCompositionLayerBaseHeader* projectionlayers[1];
	projectionlayers[0] = (XrCompositionLayerBaseHeader*)&projectionLayer;

	XrFrameEndInfo frameEndInfo = {};
	frameEndInfo.type = XR_TYPE_FRAME_END_INFO;
	frameEndInfo.displayTime = mFrameState.predictedDisplayTime;
	frameEndInfo.layerCount = 1;
	frameEndInfo.layers = projectionlayers;
	frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	if (FailMsg(xrEndFrame(mSession, &frameEndInfo), "xrEndFrame failed")) {
		Cleanup();
		return;
	}
}