#ifdef STRATUM_ENABLE_OPENXR

#include "Application.hpp"
#include "XR.hpp"
#include "Inspector.hpp"

#include <Core/Window.hpp>

namespace stm {

inline void inspector_gui_fn(XR* xrnode) {
	ImGui::LabelText("Session State", to_string(xrnode->state()).c_str());
}

XR::XR(Node& node) : mNode(node), mSwapchainImageUsage(vk::ImageUsageFlagBits::eTransferDst) {
	auto app = mNode.find_in_ancestor<Application>();
	app->PreFrame.listen(xrnode.node(), bind_front(&XR::poll_events, xrnode.get()));
	app->OnUpdate.listen(xrnode.node(), bind(&XR::render, xrnode.get(), std::placeholders::_1), EventPriority::eAlmostLast + 1024);
	app->PostFrame.listen(xrnode.node(), bind(&XR::present, xrnode.get()));

	vector<const char*> extensions{ XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };
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
	for (View& v : mViews) {
		v.mSwapchain.destroy();
		mNode.node_graph().erase(v.mTransform.node());
	}
	mViews.clear();

	if (mActionSet) {
		mPoseAction.destroy();
		mGrabAction.destroy();
		mActionSet.destroy();
		mPoseAction = XR_NULL_HANDLE;
		mGrabAction = XR_NULL_HANDLE;
		mActionSet = XR_NULL_HANDLE;
	}

	for (xr::Space& space : mActionSpaces)
		space.destroy();
	mActionSpaces.clear();

	if (mReferenceSpace) {
		mReferenceSpace.destroy();
		mReferenceSpace = XR_NULL_HANDLE;
	}

	if (mSession) {
		if (mSessionState == xr::SessionState::Synchronized || mSessionState == xr::SessionState::Visible || mSessionState == xr::SessionState::Focused || mSessionState == xr::SessionState::Stopping)
			mSession.endSession();
		mSession.destroy();
		mSession = XR_NULL_HANDLE;
	}

	//if (mDebugMessenger) mDebugMessenger.destroy();
	if (mInstance) {
		mInstance.destroy();
		mInstance = XR_NULL_HANDLE;
	}
}

void XR::get_vulkan_extensions(string& instanceExtensions, string& deviceExtensions) {
	instanceExtensions = mInstance.getVulkanInstanceExtensionsKHR(mSystem, mDispatch);
	deviceExtensions = mInstance.getVulkanDeviceExtensionsKHR(mSystem, mDispatch);
}
vk::PhysicalDevice XR::get_vulkan_device(Instance& instance) {
	return mInstance.getVulkanGraphicsDeviceKHR(mSystem, *instance, mDispatch);
}

void XR::create_session(Instance& instance) {
	mNode.node_graph().find_components<Inspector>().front()->register_inspector_gui_fn<XR>(&inspector_gui_fn);

	xr::GraphicsRequirementsVulkanKHR requirements = mInstance.getVulkanGraphicsRequirementsKHR(mSystem, mDispatch);
	if (VK_VERSION_MAJOR(instance.vulkan_version()) > requirements.maxApiVersionSupported.major() || VK_VERSION_MAJOR(instance.vulkan_version()) < requirements.minApiVersionSupported.major())
		throw runtime_error("Invalid Vulkan version");

	mQueueFamily = instance.window().present_queue_family();

	xr::GraphicsBindingVulkanKHR binding = {};
	binding.instance = (VkInstance)*instance;
	binding.physicalDevice = instance.device().physical();
	binding.device = (VkDevice)*instance.device();
	binding.queueFamilyIndex = mQueueFamily->mFamilyIndex;
	binding.queueIndex = 0;

	xr::SessionCreateInfo sessioninfo = {};
	sessioninfo.systemId = mSystem;
	sessioninfo.next = &binding;
	mSession = mInstance.createSession(sessioninfo);
	mSessionRunning = false;

	// prefer Stage, then Local, then View
	xr::ReferenceSpaceCreateInfo referenceSpaceInfo = {};
	referenceSpaceInfo.referenceSpaceType = xr::ReferenceSpaceType::View;
	for (xr::ReferenceSpaceType space : mSession.enumerateReferenceSpacesToVector())
		if (space == xr::ReferenceSpaceType::Stage) {
			referenceSpaceInfo.referenceSpaceType = space;
			break;
		} else if (space == xr::ReferenceSpaceType::Local && referenceSpaceInfo.referenceSpaceType != xr::ReferenceSpaceType::Stage)
			referenceSpaceInfo.referenceSpaceType = space;
		referenceSpaceInfo.poseInReferenceSpace.position = xr::Vector3f();
		referenceSpaceInfo.poseInReferenceSpace.orientation = xr::Quaternionf();
		mReferenceSpace = mSession.createReferenceSpace(referenceSpaceInfo);

		mPrimaryViewConfiguration = xr::ViewConfigurationType::PrimaryMono;
		for (xr::ViewConfigurationType config : mInstance.enumerateViewConfigurationsToVector(mSystem))
			if (config == xr::ViewConfigurationType::PrimaryStereo)
				mPrimaryViewConfiguration = config;

#pragma region Create actions
		xr::ActionSetCreateInfo setInfo;
		strcpy(setInfo.actionSetName, "actionset");
		strcpy(setInfo.localizedActionSetName, "Default Action Set");
		mActionSet = mInstance.createActionSet(setInfo);

		mHandPaths = {
			mInstance.stringToPath("/user/hand/left"),
			mInstance.stringToPath("/user/hand/right")
		};

		xr::ActionCreateInfo actionInfo;
		actionInfo.actionType = xr::ActionType::FloatInput;
		strcpy(actionInfo.actionName, "triggergrab"); // assuming every controller has some form of main "trigger" button
		strcpy(actionInfo.localizedActionName, "Grab Object with Trigger Button");
		actionInfo.countSubactionPaths = mHandPaths.size();
		actionInfo.subactionPaths = mHandPaths.data();
		mGrabAction = mActionSet.createAction(actionInfo);

		actionInfo.actionType = xr::ActionType::PoseInput;
		strcpy(actionInfo.actionName, "handpose");
		strcpy(actionInfo.localizedActionName, "Hand Pose");
		actionInfo.countSubactionPaths = mHandPaths.size();
		actionInfo.subactionPaths = mHandPaths.data();
		mPoseAction = mActionSet.createAction(actionInfo);

		xr::ActionSpaceCreateInfo actionSpaceInfo;
		actionSpaceInfo.action = mPoseAction;
		actionSpaceInfo.poseInActionSpace.position = xr::Vector3f();
		actionSpaceInfo.poseInActionSpace.orientation = xr::Quaternionf();
		mActionSpaces.resize(mHandPaths.size());
		for (uint32_t i = 0; i < mActionSpaces.size(); i++) {
			actionSpaceInfo.subactionPath = mHandPaths[i];
			mActionSpaces[i] = mSession.createActionSpace(actionSpaceInfo);
		}

		vector<xr::ActionSuggestedBinding> bindings{
			{ mPoseAction, mInstance.stringToPath("/user/hand/left/input/grip/pose") },
			{ mPoseAction, mInstance.stringToPath("/user/hand/right/input/grip/pose") },
			{ mGrabAction, mInstance.stringToPath("/user/hand/right/input/grip/pose") },
			{ mGrabAction, mInstance.stringToPath("/user/hand/left/input/select/click") },
			{ mGrabAction, mInstance.stringToPath("/user/hand/right/input/select/click") }
		};
		xr::InteractionProfileSuggestedBinding suggestedBindings;
		suggestedBindings.interactionProfile = mInstance.stringToPath("/interaction_profiles/khr/simple_controller");
		suggestedBindings.countSuggestedBindings = bindings.size();
		suggestedBindings.suggestedBindings = bindings.data();
		mInstance.suggestInteractionProfileBindings(suggestedBindings);

		xr::SessionActionSetsAttachInfo attachInfo;
		attachInfo.countActionSets = 1;
		attachInfo.actionSets = &mActionSet;
		mSession.attachSessionActionSets(attachInfo);
#pragma endregion
}

void XR::create_views() {
	for (View& v : mViews) {
		v.mSwapchain.destroy();
		mNode.node_graph().erase(v.mTransform.node());
	}

	const unordered_map<vk::ImageUsageFlagBits, xr::SwapchainUsageFlagBits> usageMap{
		{ vk::ImageUsageFlagBits::eColorAttachment, xr::SwapchainUsageFlagBits::ColorAttachment },
		{ vk::ImageUsageFlagBits::eDepthStencilAttachment, xr::SwapchainUsageFlagBits::DepthStencilAttachment },
		{ vk::ImageUsageFlagBits::eStorage, xr::SwapchainUsageFlagBits::UnorderedAccess },
		{ vk::ImageUsageFlagBits::eTransferSrc, xr::SwapchainUsageFlagBits::TransferSrc },
		{ vk::ImageUsageFlagBits::eTransferDst, xr::SwapchainUsageFlagBits::TransferDst },
		{ vk::ImageUsageFlagBits::eSampled, xr::SwapchainUsageFlagBits::Sampled },
		{ vk::ImageUsageFlagBits::eInputAttachment, xr::SwapchainUsageFlagBits::InputAttachmentBitMND }
	};

	xr::SwapchainCreateInfo swapchainInfo = {};
	for (const auto& [vkusage, xrusage] : usageMap)
		if (mSwapchainImageUsage & vkusage)
			swapchainInfo.usageFlags |= xrusage;
	swapchainInfo.faceCount = 1;
	swapchainInfo.arraySize = 1;
	swapchainInfo.mipCount = 1;
	swapchainInfo.sampleCount = 1;

	vector<int64_t> formats = mSession.enumerateSwapchainFormatsToVector();
	swapchainInfo.format = formats[0];
	for (uint32_t i = 0; i < formats.size(); i++)
		if ((vk::Format)formats[i] == vk::Format::eR16G16B16A16Sfloat) {
			swapchainInfo.format = formats[i];
			break;
		}

	vector<xr::ViewConfigurationView> views = mInstance.enumerateViewConfigurationViewsToVector(mSystem, mPrimaryViewConfiguration);
	mViews.resize(views.size());
	for (const xr::ViewConfigurationView& view : views) {
		View& v = mViews.emplace_back();
		Node& n = mNode.make_child("View");
		v.mCamera = n.make_component<Camera>();
		v.mTransform = n.make_component<TransformData>();
		v.mCamera->mImageRect = vk::Rect2D{ { 0, 0 }, { view.recommendedImageRectWidth, view.recommendedImageRectHeight } };

		swapchainInfo.width = view.recommendedImageRectWidth;
		swapchainInfo.height = view.recommendedImageRectHeight;
		v.mSwapchain = mSession.createSwapchain(swapchainInfo);

		vector<xr::SwapchainImageVulkanKHR> images = v.mSwapchain.enumerateSwapchainImagesToVector<xr::SwapchainImageVulkanKHR>();
		v.mSwapchainImages.reserve(images.size());
		for (uint32_t i = 0; i < images.size(); i++) {
			mQueueFamily->mDevice.set_debug_name((vk::Image)images[i].image, "xr swapchain" + to_string(i));
			v.mSwapchainImages.emplace_back(
				make_shared<Image>((vk::Image)images[i].image, mQueueFamily->mDevice, "xr swapchain" + to_string(i), vk::Extent3D(swapchainInfo.width, swapchainInfo.height, 1), (vk::Format)swapchainInfo.format, swapchainInfo.arraySize, swapchainInfo.mipCount, (vk::SampleCountFlagBits)swapchainInfo.sampleCount, mSwapchainImageUsage),
				0, 1, 0, 1, vk::ImageAspectFlagBits::eColor);
		}
	}
}

void XR::poll_events() {
	if (!mInstance) return;

	xr::EventDataBuffer event;
	if (mInstance.pollEvent(event) == xr::Result::Success) {
		switch (event.type) {
		case xr::StructureType::EventDataEventsLost:
			// lost an event
			cout << "Lost " << ((xr::EventDataEventsLost*)&event)->lostEventCount << " events" << endl;
			break;

		case xr::StructureType::EventDataSessionStateChanged: {
			xr::EventDataSessionStateChanged* state = (xr::EventDataSessionStateChanged*)&event;
			mSessionState = state->state;
			switch (state->state) {
			case xr::SessionState::Ready:
				mSession.beginSession({ mPrimaryViewConfiguration });
				mSessionRunning = true;
				break;
			case xr::SessionState::Synchronized:
			case xr::SessionState::Visible:
			case xr::SessionState::Focused:
				break;

			case xr::SessionState::Stopping:
				mSession.endSession();
				mSessionRunning = false;
				return;
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
	}

	if (mSessionRunning) {
		xr::ActiveActionSet activeActionSet = {};
		activeActionSet.actionSet = mActionSet;

		xr::ActionsSyncInfo syncInfo = {};
		syncInfo.countActiveActionSets = 1;
		syncInfo.activeActionSets = &activeActionSet;
		mSession.syncActions(syncInfo);

		xr::ActionStateFloat grabValue = mSession.getActionStateFloat({ mGrabAction, mHandPaths[0] });
		xr::ActionStatePose poseState = mSession.getActionStatePose({ mPoseAction, mHandPaths[0] });
		// TODO: handle input
	}
}

void XR::render(CommandBuffer& commandBuffer) {
	if (!mInstance || !mSessionRunning) return;

	mFrameState = mSession.waitFrame({});

	mSession.beginFrame({});

	if (mFrameState.shouldRender) {
		if (mViews.empty() || mViews[0].mSwapchainImages[0].image()->usage() != mSwapchainImageUsage)
			create_views();

		// update swapchain images
		for (auto& view : mViews) {
			view.mImageIndex = view.mSwapchain.acquireSwapchainImage({});
			view.mSwapchain.waitSwapchainImage({ xr::Duration{ XR_INFINITE_DURATION } });
		}

		// update views
		xr::ViewState viewState;
		mXRViews = mSession.locateViewsToVector({ mPrimaryViewConfiguration, mFrameState.predictedDisplayTime, mReferenceSpace }, reinterpret_cast<XrViewState*>(&viewState));
		for (uint32_t i = 0; i < mXRViews.size(); i++) {
			*mViews[i].mTransform = make_transform(
				float3(mXRViews[i].pose.position.x, mXRViews[i].pose.position.y, mXRViews[i].pose.position.z),
				make_quatf(mXRViews[i].pose.orientation.x, mXRViews[i].pose.orientation.y, mXRViews[i].pose.orientation.z, mXRViews[i].pose.orientation.w),
				float3::Ones());
			const float tanAngleLeft = tanf(mXRViews[i].fov.angleLeft);
			const float tanAngleRight = tanf(mXRViews[i].fov.angleRight);
			const float tanAngleDown = tanf(mXRViews[i].fov.angleDown);
			const float tanAngleUp = tanf(mXRViews[i].fov.angleUp);
			const float tanAngleWidth = tanAngleRight - tanAngleLeft;
			const float tanAngleHeight = tanAngleUp - tanAngleDown;
			mViews[i].mCamera->mProjection = make_perspective(
				mXRViews[i].fov.angleUp - mXRViews[i].fov.angleDown, tanAngleHeight / tanAngleWidth,
				float2((tanAngleRight + tanAngleLeft) / tanAngleWidth, (tanAngleUp + tanAngleDown) / tanAngleHeight), .01f);
		}

		vector<xr::SpaceLocation> spaceLocation(mActionSpaces.size());
		for (uint32_t i = 0; i < mActionSpaces.size(); i++)
			spaceLocation[i] = mActionSpaces[i].locateSpace(mReferenceSpace, mFrameState.predictedDisplayTime);

		OnRender(commandBuffer);

		for (View& view : mViews)
			view.back_buffer().transition_barrier(commandBuffer, vk::ImageLayout::eColorAttachmentOptimal);
	}
}

void XR::present() {
	if (!mInstance || !mSessionRunning)
		return;

	vector<xr::CompositionLayerBaseHeader*> compositionLayers;
	vector<xr::CompositionLayerProjectionView> projectionViews;
	if (mFrameState.shouldRender) {
		projectionViews.resize(mViews.size());
		for (uint32_t i = 0; i < mViews.size(); i++) {
			projectionViews[i].pose = mXRViews[i].pose;
			projectionViews[i].fov = mXRViews[i].fov;
			projectionViews[i].subImage.swapchain = mViews[i].mSwapchain;
			projectionViews[i].subImage.imageRect = xr::Rect2Di(
				xr::Offset2Di((int32_t)mViews[i].mCamera->mImageRect.offset.x, (int32_t)mViews[i].mCamera->mImageRect.offset.y),
				xr::Extent2Di((int32_t)mViews[i].mCamera->mImageRect.extent.width, (int32_t)mViews[i].mCamera->mImageRect.extent.height));
			mViews[i].mSwapchain.releaseSwapchainImage({});
		}
		mCompositionLayer.space = mReferenceSpace;
		mCompositionLayer.viewCount = (uint32_t)projectionViews.size();
		mCompositionLayer.views = projectionViews.data();
		compositionLayers.push_back(&mCompositionLayer);
	}

	xr::FrameEndInfo frameEndInfo = {};
	frameEndInfo.displayTime = mFrameState.predictedDisplayTime;
	frameEndInfo.environmentBlendMode = xr::EnvironmentBlendMode::Opaque;
	frameEndInfo.layerCount = compositionLayers.size();
	frameEndInfo.layers = compositionLayers.data();
	mSession.endFrame(frameEndInfo);
}

}

#endif