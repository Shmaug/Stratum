#include <Core/PluginManager.hpp>
#include <Input/InputManager.hpp>
#include <Scene/GUI.hpp>
#include <Scene/Scene.hpp>
#include <Util/Profiler.hpp>

using namespace std;

class Stratum {
private:
	Instance* mInstance;
	InputManager* mInputManager;
	PluginManager* mPluginManager;
	Scene* mScene;

	void Render(CommandBuffer* commandBuffer) {
		PROFILER_BEGIN("Render Cameras");
		for (Camera* camera : mScene->Cameras())
			if (camera->EnabledHierarchy())
				mScene->Render(commandBuffer, camera, PASS_MAIN);

		// Copy to window
		for (Camera* camera : mScene->Cameras())
			if (camera->TargetWindow() && camera->EnabledHierarchy() && camera->TargetWindow()->BackBuffer() != VK_NULL_HANDLE) {
				commandBuffer->TransitionBarrier(camera->TargetWindow()->BackBuffer(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
				camera->Framebuffer()->ResolveColor(commandBuffer, 0, camera->TargetWindow()->BackBuffer());
				commandBuffer->TransitionBarrier(camera->TargetWindow()->BackBuffer(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
			}

		PROFILER_END;
	}

public:
	Stratum(int argc, char** argv) {
		printf("Initializing...\n");
		mPluginManager = new PluginManager();
		mPluginManager->LoadPlugins();
		mInputManager = new InputManager();
		mInstance = new Instance(argc, argv, mPluginManager);
		printf("Initialized.\n");

		mScene = new Scene(mInstance, mInputManager, mPluginManager);
		GUI::Initialize(mInstance->Device(), mInputManager);
		mInputManager->RegisterInputDevice(mInstance->Window()->mInput);
	}

	Stratum* Loop() {
		mPluginManager->InitPlugins(mScene);
		if (mInstance->mXRRuntime) mInstance->mXRRuntime->InitScene(mScene);

		while (true) {
			#ifdef PROFILER_ENABLE
			Profiler::FrameStart(mInstance->mDevice->FrameCount());
			#endif

			PROFILER_BEGIN("Poll Events");
			for (InputDevice* d : mInputManager->mInputDevices)
				d->NextFrame();
			if (!mInstance->PollEvents()) {
				PROFILER_END;
				break;
			}
			PROFILER_END;

			if (mInstance->mXRRuntime) mInstance->mXRRuntime->BeginFrame();

			PROFILER_BEGIN("Acquire Image");
			mInstance->Window()->AcquireNextImage();
			PROFILER_END;

			CommandBuffer* commandBuffer = mInstance->Device()->GetCommandBuffer();

			mScene->Update(commandBuffer);
			Render(commandBuffer);
			if (mInstance->mXRRuntime) mInstance->mXRRuntime->PostRender(commandBuffer);

			PROFILER_BEGIN("Execute CommandBuffer");
			mInstance->Device()->Execute(commandBuffer);
			PROFILER_END;
			
			PROFILER_BEGIN("PrePresent");
			for (const auto& p : mPluginManager->Plugins()) if (p->mEnabled) p->PrePresent();
			if (mInstance->mXRRuntime) mInstance->mXRRuntime->EndFrame();
			PROFILER_END;
			
			vector<VkSemaphore> waitSemaphores;
			for (auto& kp : commandBuffer->mWaitSemaphores) waitSemaphores.push_back(*kp.second);
			mInstance->mWindow->Present(waitSemaphores);

			mInstance->Device()->TrimPool();

			#ifdef PROFILER_ENABLE
			Profiler::FrameEnd();
			#endif
		}

		mInstance->Device()->Flush();
		mPluginManager->UnloadPlugins();

		return this;
	}

	~Stratum() {
		safe_delete(mPluginManager);
				
		safe_delete(mInstance->mXRRuntime);
		safe_delete(mScene);
		safe_delete(mInputManager);
		safe_delete(mInstance);

		#ifdef PROFILER_ENABLE
		Profiler::Destroy();
		#endif
	}
};

int main(int argc, char** argv) {
	#ifdef WINDOWS
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		fprintf_color(COLOR_RED, stderr, "Error: WSAStartup failed\n");
	#endif

	// create, run, and delete the engine all in one line :)
	delete (new Stratum(argc, argv))->Loop();

	#ifdef WINDOWS
	WSACleanup();
	#endif

	#if defined(WINDOWS)
	OutputDebugString("Dumping Memory Leaks...\n");
	_CrtDumpMemoryLeaks();
	OutputDebugString("Done\n");
	#endif
	return EXIT_SUCCESS;
}