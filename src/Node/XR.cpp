#include "XR.hpp"

using namespace stm;
using namespace stm::hlsl;

XR::XR(Node& node) : mNode(node), mSwapchainImageUsage(vk::ImageUsageFlagBits::eTransferDst) {
	vector<const char*> extensions { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };
	xr::InstanceCreateInfo instanceInfo = {};
	instanceInfo.applicationInfo.apiVersion = xr::Version::current();
	strcpy(instanceInfo.applicationInfo.engineName, "Stratum");
	strcpy(instanceInfo.applicationInfo.applicationName, "Stratum");
	instanceInfo.applicationInfo.engineVersion = VK_MAKE_VERSION(STRATUM_VERSION_MAJOR, STRATUM_VERSION_MINOR, 0);
	instanceInfo.applicationInfo.applicationVersion = instanceInfo.applicationInfo.engineVersion;
	instanceInfo.enabledExtensionCount = extensions.size();
	instanceInfo.enabledExtensionNames = extensions.data();
	instanceInfo.enabledApiLayerCount = 0;
  mInstance = xr::createInstance(instanceInfo);

	xr::InstanceProperties instanceProperties = mInstance.getInstanceProperties();
	cout << "OpenXR runtime: " << instanceProperties.runtimeName << endl;

  mDispatch = xr::DispatchLoaderDynamic{ mInstance };

	//xr::DebugUtilsMessengerCreateInfoEXT debugMessengerInfo = {};
	//debugMessengerInfo.messageSeverities = xr::DebugUtilsMessageSeverityFlagBitsEXT::Error | xr::DebugUtilsMessageSeverityFlagBitsEXT::Warning;
	//debugMessengerInfo.messageTypes = xr::DebugUtilsMessageTypeFlagBitsEXT::AllBits;
	//debugMessengerInfo.userCallback = DebugCallback;
	//mDebugMessenger = mInstance.createDebugUtilsMessengerEXT(debugMessengerInfo, mDispatch);

	xr::SystemGetInfo systemInfo = {};
	systemInfo.formFactor = xr::FormFactor::HeadMountedDisplay;
  mSystem = mInstance.getSystem(systemInfo);
	xr::SystemProperties systemProperties = mInstance.getSystemProperties(mSystem);
	cout << "OpenXR system: " << systemProperties.systemName << endl;
}

void XR::destroy() {
	if (mSwapchain) {
		mSwapchain.destroy();
		mSwapchainImages.clear();
	}

	if (mPoseAction) mPoseAction.destroy();
	if (mGrabAction) mGrabAction.destroy();

	if (mActionSet) xrDestroyActionSet(mActionSet);
	for (const xr::Space& space : mActionSpaces) space.destroy();
	if (mReferenceSpace) mReferenceSpace.destroy(); 

	if (mSession) {
    mSession.endSession();
    mSession.destroy();
	}

	//if (mDebugMessenger) mDebugMessenger.destroy();
	if (mInstance) mInstance.destroy();

	mInstance = XR_NULL_HANDLE;
	mSession = XR_NULL_HANDLE;
	mReferenceSpace = XR_NULL_HANDLE;
	mActionSet = XR_NULL_HANDLE;
	mPoseAction = XR_NULL_HANDLE;
	mGrabAction = XR_NULL_HANDLE;
}

void XR::get_vulkan_extensions(string& instanceExtensions, string& deviceExtensions) {
	instanceExtensions = mInstance.getVulkanInstanceExtensionsKHR(mSystem, mDispatch);
	deviceExtensions = mInstance.getVulkanDeviceExtensionsKHR(mSystem, mDispatch);
}

void XR::create_swapchain() {
	if (mSwapchain) {
		mSwapchain.destroy();
		mSwapchainImages.clear();
		for (View& v : mViews)
			mNode.node_graph().erase(v.mTransform.node());
	}

	const unordered_map<vk::ImageUsageFlagBits, xr::SwapchainUsageFlagBits> usageMap {
		{ vk::ImageUsageFlagBits::eColorAttachment, xr::SwapchainUsageFlagBits::ColorAttachment },
		{ vk::ImageUsageFlagBits::eDepthStencilAttachment, xr::SwapchainUsageFlagBits::DepthStencilAttachment },
		{ vk::ImageUsageFlagBits::eStorage, xr::SwapchainUsageFlagBits::UnorderedAccess },
		{ vk::ImageUsageFlagBits::eTransferSrc, xr::SwapchainUsageFlagBits::TransferSrc },
		{ vk::ImageUsageFlagBits::eTransferDst, xr::SwapchainUsageFlagBits::TransferDst },
		{ vk::ImageUsageFlagBits::eSampled, xr::SwapchainUsageFlagBits::Sampled },
		{ vk::ImageUsageFlagBits::eInputAttachment, xr::SwapchainUsageFlagBits::InputAttachmentBitMND }
	};

	xr::SwapchainCreateInfo swapchainInfo = {};
	for (const auto&[vkusage, xrusage] : usageMap)
		if (mSwapchainImageUsage & vkusage)
			swapchainInfo.usageFlags |= xrusage;	
	swapchainInfo.faceCount = 1;
	swapchainInfo.arraySize = 1;
	swapchainInfo.mipCount = 1;
	swapchainInfo.sampleCount = 1;
	swapchainInfo.width = 0;
	swapchainInfo.height = 0;
	vector<xr::ViewConfigurationView> views = mInstance.enumerateViewConfigurationViews(mSystem, mPrimaryViewConfiguration);
	mViews.reserve(views.size());
	for (const xr::ViewConfigurationView& view : views) {
		View& v = mViews.emplace_back();
		if (swapchainInfo.width == view.recommendedImageRectWidth) {
			v.mImageRect = vk::Rect2D{
				{ (int32_t)view.recommendedImageRectWidth, (int32_t)view.recommendedImageRectHeight },
				{ 0, swapchainInfo.height } };
			swapchainInfo.height += view.recommendedImageRectHeight;
		} else if (swapchainInfo.height == view.recommendedImageRectHeight) {
			v.mImageRect = vk::Rect2D{
				{ (int32_t)view.recommendedImageRectWidth, (int32_t)view.recommendedImageRectHeight },
				{ swapchainInfo.width, 0 } };
			swapchainInfo.width += view.recommendedImageRectWidth;
		} else {
			v.mImageRect = vk::Rect2D{
				{ (int32_t)view.recommendedImageRectWidth, (int32_t)view.recommendedImageRectHeight },
				{ 0, 0 } };
			swapchainInfo.width = view.recommendedImageRectWidth;
			swapchainInfo.height = view.recommendedImageRectHeight;
		}
		
		Node& n = mNode.make_child("View");
		v.mCamera = n.make_component<Camera>();
		v.mTransform = n.make_component<TransformData>();
	}

	vector<int64_t> formats = mSession.enumerateSwapchainFormats();
	swapchainInfo.format = formats[0];
	for (uint32_t i = 0; i < formats.size(); i++)
		if ((vk::Format)formats[i] == vk::Format::eR8G8B8A8Unorm) {
			swapchainInfo.format = formats[i];
			break;
		} else if ((vk::Format)formats[i] == vk::Format::eB8G8R8A8Unorm)
			swapchainInfo.format = formats[i];

	mSwapchain = mSession.createSwapchain(swapchainInfo);

	vector<xr::SwapchainImageVulkanKHR> images = mSwapchain.enumerateSwapchainImages<xr::SwapchainImageVulkanKHR>();
	mSwapchainImages.resize(images.size());	
	for (uint32_t i = 0; i < images.size(); i++)
		mSwapchainImages[i] = make_shared<Image>((vk::Image)images[i].image, mQueueFamily->mDevice, "XR Swapchain Image " + to_string(i),
			vk::Extent3D(swapchainInfo.width, swapchainInfo.height, 1), (vk::Format)swapchainInfo.format, swapchainInfo.arraySize, swapchainInfo.mipCount, (vk::SampleCountFlagBits)swapchainInfo.sampleCount,
			mSwapchainImageUsage, vk::ImageType::e2D);
}

void XR::create_session(Device::QueueFamily& queueFamily) {
	auto instance = mNode.find_in_ancestor<Instance>();

	xr::GraphicsBindingVulkanKHR binding = {};
	binding.instance = **instance;
	binding.physicalDevice = instance->device().physical();
	binding.device = *instance->device();
	binding.queueFamilyIndex = queueFamily.mFamilyIndex;
	binding.queueIndex = 0;
	mQueueFamily = &queueFamily;
	
	xr::SessionCreateInfo sessioninfo = {};
	sessioninfo.systemId = mSystem;
	sessioninfo.next = &binding;
	mSession = mInstance.createSession(sessioninfo);

	// prefer Stage, then Local, then View
	xr::ReferenceSpaceCreateInfo referenceSpaceInfo = {};
	referenceSpaceInfo.referenceSpaceType = xr::ReferenceSpaceType::View;
	for (xr::ReferenceSpaceType space : mSession.enumerateReferenceSpaces())
		if (space == xr::ReferenceSpaceType::Stage) {
			referenceSpaceInfo.referenceSpaceType = space;
			break;
		} else if (space == xr::ReferenceSpaceType::Local && referenceSpaceInfo.referenceSpaceType != xr::ReferenceSpaceType::Stage)
			referenceSpaceInfo.referenceSpaceType = space;
	referenceSpaceInfo.poseInReferenceSpace.orientation = { 0.f, 0.f, 0.f, 1.f };
	referenceSpaceInfo.poseInReferenceSpace.position = { 0.f, 0.f, 0.f };
	mReferenceSpace = mSession.createReferenceSpace(referenceSpaceInfo);

	mPrimaryViewConfiguration = xr::ViewConfigurationType::PrimaryMono;
	for (xr::ViewConfigurationType config : mInstance.enumerateViewConfigurations(mSystem))
		if (config == xr::ViewConfigurationType::PrimaryStereo)
			mPrimaryViewConfiguration = config;

	mSession.beginSession({ mPrimaryViewConfiguration });
	
	create_swapchain();

	#pragma region Create actions
	xr::ActionSetCreateInfo setInfo;
	strcpy(setInfo.actionSetName, "actionset");
	strcpy(setInfo.localizedActionSetName, "Action Set");
	mActionSet = mInstance.createActionSet(setInfo);

	mActionSpaces.resize(2); // 2 hands
	mHandPaths.resize(2);
	mHandPaths[0] = mInstance.stringToPath("/user/hand/left");
	mHandPaths[1] = mInstance.stringToPath("/user/hand/right");

	xr::ActionCreateInfo actionInfo;
	actionInfo.actionType = xr::ActionType::FloatInput;
	actionInfo.countSubactionPaths = mHandPaths.size();
	actionInfo.subactionPaths = mHandPaths.data();
	strcpy(actionInfo.actionName, "triggergrab"); // assuming every controller has some form of main "trigger" button
	strcpy(actionInfo.localizedActionName, "Grab Object with Trigger Button");
	mGrabAction = mActionSet.createAction(actionInfo);

	actionInfo.actionType = xr::ActionType::PoseInput;
	strcpy(actionInfo.actionName, "handpose");
	strcpy(actionInfo.localizedActionName, "Hand Pose");
	actionInfo.countSubactionPaths = mHandPaths.size();
	actionInfo.subactionPaths = mHandPaths.data();
	mPoseAction = mActionSet.createAction(actionInfo);

	vector<xr::ActionSuggestedBinding> bindings(4);
	bindings[0].action = mPoseAction;
	bindings[0].binding = mInstance.stringToPath("/user/hand/left/input/grip/pose");
	bindings[1].action = mPoseAction;
	bindings[1].binding = mInstance.stringToPath("/user/hand/right/input/grip/pose");
	bindings[2].action = mGrabAction;
	bindings[2].binding = mInstance.stringToPath("/user/hand/left/input/select/click");
	bindings[3].action = mGrabAction;
	bindings[3].binding = mInstance.stringToPath("/user/hand/right/input/select/click");

	xr::InteractionProfileSuggestedBinding suggestedBindings;
	suggestedBindings.interactionProfile = mInstance.stringToPath("/interaction_profiles/khr/simple_controller");
	suggestedBindings.countSuggestedBindings = bindings.size();
	suggestedBindings.suggestedBindings = bindings.data();
	mInstance.suggestInteractionProfileBindings(suggestedBindings);

	// Create action space for each hand
	xr::ActionSpaceCreateInfo actionSpaceInfo;
	actionSpaceInfo.action = mPoseAction;
	actionSpaceInfo.poseInActionSpace.position = { 0.f, 0.f, 0.f };
	actionSpaceInfo.poseInActionSpace.orientation = { 0.f, 0.f, 0.f, 1.f };
	for (uint32_t i = 0; i < mActionSpaces.size(); i++) {
		actionSpaceInfo.subactionPath = mHandPaths[i];
		mActionSpaces[i] = mSession.createActionSpace(actionSpaceInfo);
	}

	xr::SessionActionSetsAttachInfo attachInfo;
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &mActionSet;
	mSession.attachSessionActionSets(attachInfo);
	#pragma endregion
}

void XR::poll_actions() {
	if (!mInstance) return;

	if (mSwapchainImages[0]->usage() != mSwapchainImageUsage)
		create_swapchain();

	xr::EventDataBuffer event;
	mInstance.pollEvent(event);
	switch (event.type) {
	case xr::StructureType::EventDataEventsLost:
		// lost an event
		break;

	case xr::StructureType::EventDataSessionStateChanged: {
		xr::EventDataSessionStateChanged* state = (xr::EventDataSessionStateChanged*)&event;
		switch (state->state) {
		default:
		case xr::SessionState::Unknown:
		case xr::SessionState::Idle:

		case xr::SessionState::Ready:
		case xr::SessionState::Synchronized:
		case xr::SessionState::Visible:
		case xr::SessionState::Focused:
			break;

		case xr::SessionState::Stopping:
		case xr::SessionState::Exiting:
		case xr::SessionState::LossPending:
			destroy();
			return;
		}
		break;
	}
	case xr::StructureType::EventDataInstanceLossPending:
		destroy();
		return;
	}

	xr::ActiveActionSet activeActionSet = {};
	activeActionSet.actionSet = mActionSet;

	xr::ActionsSyncInfo syncInfo = {};
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeActionSet;
	mSession.syncActions(syncInfo);

	xr::ActionStateFloat grabValue = mSession.getActionStateFloat({ mGrabAction, mHandPaths[0] });
	xr::ActionStatePose poseState = mSession.getActionStatePose({ mPoseAction, mHandPaths[0] });
}

void XR::do_frame(CommandBuffer& commandBuffer) {
	if (!mInstance) return;
	
	xr::FrameState frameState = mSession.waitFrame({});
	mSession.beginFrame({});

	mSwapchainImageIndex = mSwapchain.acquireSwapchainImage({});
	mSwapchain.waitSwapchainImage({ xr::Duration{ 10000 }  });

	vector<xr::SpaceLocation> spaceLocation(mActionSpaces.size());
	for (uint32_t i = 0; i < mActionSpaces.size(); i++)
		spaceLocation[i] = mActionSpaces[i].locateSpace(mReferenceSpace, frameState.predictedDisplayTime);

	xr::ViewState viewState;
	vector<xr::View> views = mSession.locateViews({ mPrimaryViewConfiguration, frameState.predictedDisplayTime, mReferenceSpace }, reinterpret_cast<XrViewState*>(&viewState));

	vector<xr::CompositionLayerProjectionView> projectionViews(views.size());
	for (uint32_t i = 0; i < views.size(); i++) {
		*mViews[i].mTransform = make_transform(
			float3(views[i].pose.position.x, views[i].pose.position.y, views[i].pose.position.z),
			make_quatf(views[i].pose.orientation.x, views[i].pose.orientation.y, views[i].pose.orientation.z, views[i].pose.orientation.w),
			float3::Ones());
		// TODO: assign camera properties
		
		projectionViews[i].pose = views[i].pose;
		projectionViews[i].fov = views[i].fov;
		projectionViews[i].subImage.imageArrayIndex = mSwapchainImageIndex;
		projectionViews[i].subImage.swapchain = mSwapchain;
		projectionViews[i].subImage.imageRect = { { (int32_t)mViews[i].mImageRect.offset.x, (int32_t)mViews[i].mImageRect.offset.y }, { (int32_t)mViews[i].mImageRect.extent.width, (int32_t)mViews[i].mImageRect.extent.height } };
	}

	OnRender(commandBuffer);

	mSwapchain.releaseSwapchainImage({});

	xr::CompositionLayerProjection compositionLayer = {};
	compositionLayer.space = mReferenceSpace;
	compositionLayer.viewCount = (uint32_t)projectionViews.size();
	compositionLayer.views = projectionViews.data();

	xr::CompositionLayerBaseHeader* compositionLayerHeader = &compositionLayer;

	xr::FrameEndInfo frameEndInfo = {};
	frameEndInfo.displayTime = frameState.predictedDisplayTime;
	frameEndInfo.layerCount = 1;
	frameEndInfo.layers = &compositionLayerHeader;
	frameEndInfo.environmentBlendMode = xr::EnvironmentBlendMode::Opaque;
	mSession.endFrame(frameEndInfo);
}