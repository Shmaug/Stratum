#include <Scene/GUI.hpp>
#include <Scene/Scene.hpp>

using namespace std;

#define CIRCLE_VERTEX_RESOLUTION 64

#define START_DEPTH 0.01f
#define DEPTH_DELTA -0.001f

#define ICON_CIRCLE_ST (float4(128, 128, 256, 512) / 1024.f)
#define ICON_CHECKBOX_ST (float4(128, 128, 0, 512) / 1024.f)
#define ICON_CHECK_ST (float4(128, 128, 128, 512) / 1024.f)
#define ICON_TRI_RIGHT_ST (float4(128, 128, 384, 512) / 1024.f)
#define ICON_TRI_LEFT_ST (float4(128, 128, 512, 512) / 1024.f)

InputManager* GUI::mInputManager;

unordered_map<string, uint32_t> GUI::mHotControl;
unordered_map<string, uint32_t> GUI::mLastHotControl;
unordered_map<uint32_t, std::variant<float, std::string>> GUI::mControlData;
uint32_t GUI::mNextControlId = 10;

vector<Texture*> GUI::mTextureArray;
unordered_map<Texture*, uint32_t> GUI::mTextureMap;

vector<GUI::GuiRect> GUI::mScreenRects;
vector<GUI::GuiRect> GUI::mScreenTextureRects;
vector<GUI::GuiRect> GUI::mWorldRects;
vector<GUI::GuiRect> GUI::mWorldTextureRects;
vector<GUI::GuiLine> GUI::mScreenLines;
vector<GUI::GuiLine> GUI::mWorldLines;
vector<float3> GUI::mLinePoints;
vector<float4x4> GUI::mLineTransforms;
vector<GUI::GuiString> GUI::mScreenStrings;
vector<GUI::GuiString> GUI::mWorldStrings;
stack<GUI::GuiLayout> GUI::mLayoutStack;

Texture* GUI::mIconsTexture;

GUI::LayoutTheme GUI::mLayoutTheme;


void GUI::Initialize(Device* device, InputManager* inputManager) {
	mInputManager = inputManager;
	mNextControlId = 10;

	mIconsTexture = device->AssetManager()->LoadTexture("Assets/Textures/icons.png");

	mLayoutTheme.mBackgroundColor = float4(.21f, .21f, .21f, 1.f);
	mLayoutTheme.mControlBackgroundColor = float4(.16f, .16f, .16f, 1.f);
	mLayoutTheme.mTextColor = float4(0.9f, 0.9f, 0.9f, 1.f);
	mLayoutTheme.mButtonColor = float4(.31f, .31f, .31f, 1.f);
	mLayoutTheme.mSliderColor = float4(.36f, .36f, .36f, 1.f);
	mLayoutTheme.mSliderKnobColor = float4(.6f, .6f, .6f, 1.f);
	mLayoutTheme.mControlFont = device->AssetManager()->LoadFont("Assets/Fonts/FantasqueSansMono/FantasqueSansMono-Regular.ttf", 12);
	mLayoutTheme.mTitleFont = device->AssetManager()->LoadFont("Assets/Fonts/FantasqueSansMono/FantasqueSansMono-Bold.ttf", 18);
	mLayoutTheme.mControlSize = 16;
	mLayoutTheme.mControlPadding = 2;
	mLayoutTheme.mSliderBarSize = 2;
	mLayoutTheme.mSliderKnobSize = 10;
	mLayoutTheme.mScrollBarThickness = 6;

}

void GUI::Reset(CommandBuffer* commandBuffer) {
	mNextControlId = 10;
	mLastHotControl = mHotControl;
	mHotControl.clear();

	mTextureArray.clear();
	mTextureMap.clear();

	mWorldRects.clear();
	mWorldTextureRects.clear();
	mWorldStrings.clear();

	mScreenRects.clear();
	mScreenTextureRects.clear();
	mScreenStrings.clear();
	mScreenLines.clear();
	mLinePoints.clear();
	mLineTransforms.clear();
}
void GUI::PreBeginRenderPass(CommandBuffer* commandBuffer) {
	for (Texture* t : mTextureArray)
		commandBuffer->TransitionBarrier(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	for (uint32_t i = 0; i < mWorldStrings.size(); i++)
		commandBuffer->TransitionBarrier(mWorldStrings[i].mFont->Texture(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	for (uint32_t i = 0; i < mScreenStrings.size(); i++)
		commandBuffer->TransitionBarrier(mScreenStrings[i].mFont->Texture(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void GUI::Draw(CommandBuffer* commandBuffer, PassType pass, Camera* camera) {	
	if (mWorldRects.size()) {
		GraphicsShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/ui.stm")->GetGraphics(pass, {});
		VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr, camera);

		Buffer* screenRects = commandBuffer->GetBuffer("WorldRects", mWorldRects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		memcpy(screenRects->MappedData(), mWorldRects.data(), mWorldRects.size() * sizeof(GuiRect));

		DescriptorSet* ds = commandBuffer->GetDescriptorSet("WorldRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
		ds->CreateStorageBufferDescriptor(screenRects, 0, mWorldRects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
		ds->FlushWrites();
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

		camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
		vkCmdDraw(*commandBuffer, 6, (uint32_t)mWorldRects.size(), 0, 0);

		if (camera->StereoMode() != STEREO_NONE) {
			camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
			vkCmdDraw(*commandBuffer, 6, (uint32_t)mWorldRects.size(), 0, 0);
		}
	}
	if (mWorldTextureRects.size()) {
		GraphicsShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/ui.stm")->GetGraphics(pass, { "TEXTURED" });
		VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr, camera);

		Buffer* screenRects = commandBuffer->GetBuffer("WorldRects", mWorldTextureRects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		memcpy(screenRects->MappedData(), mWorldTextureRects.data(), mWorldTextureRects.size() * sizeof(GuiRect));

		DescriptorSet* ds = commandBuffer->GetDescriptorSet("WorldRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
		ds->CreateStorageBufferDescriptor(screenRects, 0, mWorldTextureRects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
		for (uint32_t i = 0; i < mTextureArray.size(); i++)
			ds->CreateSampledTextureDescriptor(mTextureArray[i], i, shader->mDescriptorBindings.at("Textures").second.binding);
		ds->FlushWrites();
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

		camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
		vkCmdDraw(*commandBuffer, 6, (uint32_t)mWorldTextureRects.size(), 0, 0);

		if (camera->StereoMode() != STEREO_NONE) {
			camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
			vkCmdDraw(*commandBuffer, 6, (uint32_t)mWorldTextureRects.size(), 0, 0);
		}
	}
	if (mWorldStrings.size()) {
		GraphicsShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/font.stm")->GetGraphics(PASS_MAIN, {});
		VkPipelineLayout layout = commandBuffer->BindShader(shader, PASS_MAIN, nullptr, camera);

		Buffer* transforms = commandBuffer->GetBuffer("Transforms", sizeof(float4x4) * mWorldStrings.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		float4x4* m = (float4x4*)transforms->MappedData();
		for (const GuiString& s : mWorldStrings) {
			*m = s.mTransform;
			m++;
		}
		uint32_t idx = 0;

		for (const GuiString& s : mWorldStrings) {
			/*
			size_t key = 0;
			hash_combine(key, s.mString);
			hash_combine(key, s.mFont);
			hash_combine(key, s.mHorizontalAnchor);
			hash_combine(key, s.mVerticalAnchor);
			*/

			vector<TextGlyph> glyphs(s.mString.length());
			uint32_t glyphCount = s.mFont->GenerateGlyphs(s.mString, nullptr, glyphs, s.mHorizontalAnchor, s.mVerticalAnchor);
			if (glyphCount == 0) { idx++; continue; }
			
			Buffer* glyphBuffer = commandBuffer->GetBuffer("Glyph Buffer", glyphCount * sizeof(TextGlyph), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			glyphBuffer->Upload(glyphs.data(), glyphCount * sizeof(TextGlyph));

			DescriptorSet* descriptorSet = commandBuffer->GetDescriptorSet(s.mFont->mName + " DescriptorSet", shader->mDescriptorSetLayouts[PER_OBJECT]);
			descriptorSet->CreateSampledTextureDescriptor(s.mFont->Texture(), BINDING_START + 0);
			descriptorSet->CreateStorageBufferDescriptor(transforms, 0, sizeof(float4x4) * mWorldStrings.size(), BINDING_START + 1);
			descriptorSet->CreateStorageBufferDescriptor(glyphBuffer, 0, glyphCount * sizeof(TextGlyph), BINDING_START + 2);
			descriptorSet->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *descriptorSet, 0, nullptr);

			commandBuffer->PushConstantRef(shader, "Color", s.mColor);
			commandBuffer->PushConstantRef(shader, "Offset", s.mOffset);
			commandBuffer->PushConstantRef(shader, "Bounds", s.mBounds);
			commandBuffer->PushConstantRef(shader, "Depth", s.mDepth);
			commandBuffer->PushConstantRef(shader, "Scale", s.mScale);

			camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
			vkCmdDraw(*commandBuffer, (glyphBuffer->Size() / sizeof(TextGlyph)) * 6, 1, 0, idx);
			if (camera->StereoMode() != STEREO_NONE) {
				camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
				vkCmdDraw(*commandBuffer, (glyphBuffer->Size() / sizeof(TextGlyph)) * 6, 1, 0, idx);
			}

			idx++;
		}
	}

	if (camera->StereoMode() == STEREO_NONE) {
		camera->SetViewportScissor(commandBuffer);
		
		if (mScreenRects.size()) {
			GraphicsShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/ui.stm")->GetGraphics(pass, { "SCREEN_SPACE" });
			VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr);

			Buffer* screenRects = commandBuffer->GetBuffer("ScreenRects", mScreenRects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			memcpy(screenRects->MappedData(), mScreenRects.data(), mScreenRects.size() * sizeof(GuiRect));

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("ScreenRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateStorageBufferDescriptor(screenRects, 0, mScreenRects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

			commandBuffer->PushConstantRef(shader, "ScreenSize", float2(camera->Framebuffer()->Extent().width, camera->Framebuffer()->Extent().height));

			vkCmdDraw(*commandBuffer, 6, (uint32_t)mScreenRects.size(), 0, 0);
		}
		if (mScreenTextureRects.size()) {
			GraphicsShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/ui.stm")->GetGraphics(pass, { "SCREEN_SPACE", "TEXTURED" });
			VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr);

			Buffer* screenRects = commandBuffer->GetBuffer("ScreenRects", mScreenTextureRects.size() * sizeof(GuiRect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			memcpy(screenRects->MappedData(), mScreenTextureRects.data(), mScreenTextureRects.size() * sizeof(GuiRect));

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("ScreenRects", shader->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateStorageBufferDescriptor(screenRects, 0, mScreenTextureRects.size() * sizeof(GuiRect), shader->mDescriptorBindings.at("Rects").second.binding);
			for (uint32_t i = 0; i < mTextureArray.size(); i++)
				ds->CreateSampledTextureDescriptor(mTextureArray[i], shader->mDescriptorBindings.at("Textures").second.binding, i);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

			vkCmdDraw(*commandBuffer, 6, (uint32_t)mScreenTextureRects.size(), 0, 0);
		}
		if (mScreenStrings.size()) {
			GraphicsShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/font.stm")->GetGraphics(PASS_MAIN, { "SCREEN_SPACE" });
			VkPipelineLayout layout = commandBuffer->BindShader(shader, PASS_MAIN, nullptr);
			commandBuffer->PushConstantRef(shader, "ScreenSize", float2(camera->Framebuffer()->Extent().width, camera->Framebuffer()->Extent().height));

			for (const GuiString& s : mScreenStrings) {
				/*
				size_t key = 0;
				hash_combine(key, s.mString);
				hash_combine(key, s.mFont);
				hash_combine(key, s.mHorizontalAnchor);
				hash_combine(key, s.mVerticalAnchor);
				*/
			
				vector<TextGlyph> glyphs(s.mString.length());
				uint32_t glyphCount = s.mFont->GenerateGlyphs(s.mString, nullptr, glyphs, s.mHorizontalAnchor, s.mVerticalAnchor);
				if (glyphCount == 0) continue;

				Buffer* glyphBuffer = commandBuffer->GetBuffer("Glyph Buffer", glyphCount * sizeof(TextGlyph), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
				glyphBuffer->Upload(glyphs.data(), glyphCount * sizeof(TextGlyph));

				DescriptorSet* descriptorSet = commandBuffer->GetDescriptorSet(s.mFont->mName + " DescriptorSet", shader->mDescriptorSetLayouts[PER_OBJECT]);
				descriptorSet->CreateSampledTextureDescriptor(s.mFont->Texture(), BINDING_START + 0);
				descriptorSet->CreateStorageBufferDescriptor(glyphBuffer, 0, glyphCount * sizeof(TextGlyph), BINDING_START + 2);
				descriptorSet->FlushWrites();
				vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *descriptorSet, 0, nullptr);

				commandBuffer->PushConstantRef(shader, "Color", s.mColor);
				commandBuffer->PushConstantRef(shader, "Offset", s.mOffset);
				commandBuffer->PushConstantRef(shader, "Bounds", s.mBounds);
				commandBuffer->PushConstantRef(shader, "Depth", s.mDepth);
				commandBuffer->PushConstantRef(shader, "Scale", s.mScale);
				vkCmdDraw(*commandBuffer, (glyphBuffer->Size() / sizeof(TextGlyph)) * 6, 1, 0, 0);
			}
		}
		if (mScreenLines.size()) {
			GraphicsShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/line.stm")->GetGraphics(PASS_MAIN, { "SCREEN_SPACE" });
			VkPipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr, nullptr, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);

			Buffer* pts = commandBuffer->GetBuffer("Line Pts", sizeof(float3) * mLinePoints.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			Buffer* transforms = commandBuffer->GetBuffer("Line Transforms", sizeof(float4x4) * mLineTransforms.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			
			memcpy(transforms->MappedData(), mLineTransforms.data(), sizeof(float4x4) * mLineTransforms.size());
			memcpy(pts->MappedData(), mLinePoints.data(), sizeof(float3) * mLinePoints.size());
			
			DescriptorSet* ds = commandBuffer->GetDescriptorSet("ScreenLines", shader->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateStorageBufferDescriptor(pts, 0, sizeof(float3) * mLinePoints.size(), INSTANCE_BUFFER_BINDING);
			ds->CreateStorageBufferDescriptor(transforms, 0, sizeof(float4x4) * mLineTransforms.size(), BINDING_START);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *ds, 0, nullptr);

			commandBuffer->PushConstantRef(shader, "ScreenSize", float2(camera->Framebuffer()->Extent().width, camera->Framebuffer()->Extent().height));

			for (const GuiLine& l : mScreenLines) {
				commandBuffer->PushConstantRef(shader, "Color", l.mColor);
				commandBuffer->PushConstantRef(shader, "Bounds", l.mBounds);
				commandBuffer->PushConstantRef(shader, "TransformIndex", l.mTransformIndex);
				vkCmdSetLineWidth(*commandBuffer, l.mThickness);
				vkCmdDraw(*commandBuffer, l.mCount, 1, l.mIndex, 0);
			}
		}
	}
	
	mWorldRects.clear();
	mWorldTextureRects.clear();
	mWorldStrings.clear();

	mScreenRects.clear();
	mScreenTextureRects.clear();
	mScreenStrings.clear();
	mScreenLines.clear();
	mLinePoints.clear();

	while (mLayoutStack.size()) mLayoutStack.pop();
}


void GUI::PolyLine(const float3* points, uint32_t pointCount, const float4& color, float thickness, const float3& offset, const float3& scale, float z, const fRect2D& clipRect) {
	GuiLine l;
	l.mColor = color;
	l.mBounds = clipRect;
	l.mCount = pointCount;
	l.mIndex = mLinePoints.size();
	l.mThickness = thickness;
	l.mTransformIndex = mLineTransforms.size();
	mScreenLines.push_back(l);
	mLineTransforms.push_back(float4x4::TRS(offset + float3(0, 0, z), quaternion(0,0,0,1), scale));

	mLinePoints.resize(mLinePoints.size() + pointCount);
	memcpy(mLinePoints.data() + l.mIndex, points, pointCount * sizeof(float3));
}
void GUI::PolyLine(const float4x4& transform, const float3* points, uint32_t pointCount, const float4& color, float thickness, const fRect2D& clipRect) {
	GuiLine l;
	l.mColor = color;
	l.mBounds = clipRect;
	l.mThickness = thickness;
	l.mIndex = mLinePoints.size();
	l.mCount = pointCount;
	l.mTransformIndex = mLineTransforms.size();
	mWorldLines.push_back(l);
	mLineTransforms.push_back(transform);

	mLinePoints.resize(mLinePoints.size() + pointCount);
	memcpy(mLinePoints.data() + l.mIndex, points, pointCount * sizeof(float3));
}

void GUI::WireCube(const float3& center, const float3& extents, const quaternion& rotation, const float4& color) {
	// TODO
}
void GUI::WireCircle(const float3& center, float radius, const quaternion& rotation, const float4& color) {
	// TODO
}
void GUI::WireSphere(const float3& center, float radius, const float4& color) {
	WireCircle(center, radius, quaternion(0,0,0,1), color);
	WireCircle(center, radius, quaternion(0, .70710678f, 0, .70710678f), color);
	WireCircle(center, radius, quaternion(.70710678f, 0, 0, .70710678f), color);
}


bool GUI::PositionHandle(const string& name, const quaternion& plane, float3& position, float radius, const float4& color) {
	WireCircle(position, radius, plane, color);
	
	size_t controlId = hash<string>()(name);
	bool ret = false;

	for (const InputDevice* d : mInputManager->InputDevices())
		for (uint32_t i = 0; i < d->PointerCount(); i++) {
			const InputPointer* p = d->GetPointer(i);
			const InputPointer* lp = d->GetPointerLast(i);

			float2 t, lt;
			bool hit = p->mWorldRay.Intersect(Sphere(position, radius), t);
			bool hitLast = lp->mWorldRay.Intersect(Sphere(position, radius), lt);

			if (!p->mPrimaryButton) {
				mHotControl.erase(p->mName);
				continue;
			}
			if ((mHotControl.count(p->mName) == 0 || mHotControl.at(p->mName) != controlId) && (!hit || !hitLast || t.x < 0 || lt.x < 0)) continue;
			mHotControl[p->mName] = controlId;

			float3 fwd = plane * float3(0,0,1);
			float3 pos  = p->mWorldRay.mOrigin + p->mWorldRay.mDirection * p->mWorldRay.Intersect(fwd, position);
			float3 posLast = lp->mWorldRay.mOrigin + lp->mWorldRay.mDirection * lp->mWorldRay.Intersect(fwd, position);

			position += pos - posLast;

			ret = true;
		}
	
	return ret;
}
bool GUI::RotationHandle(const string& name, const float3& center, quaternion& rotation, float radius, float sensitivity) {
	quaternion r = rotation;
	WireCircle(center, radius, r, float4(.2f,.2f,1,.5f));
	r *= quaternion(float3(0, PI/2, 0));
	WireCircle(center, radius, r, float4(1,.2f,.2f,.5f));
	r *= quaternion(float3(PI/2, 0, 0));
	WireCircle(center, radius, r, float4(.2f,1,.2f,.5f));

	size_t controlId = hash<string>()(name);
	bool ret = false;

	for (const InputDevice* d : mInputManager->InputDevices())
		for (uint32_t i = 0; i < d->PointerCount(); i++) {
			const InputPointer* p = d->GetPointer(i);
			const InputPointer* lp = d->GetPointerLast(i);

			float2 t, lt;
			bool hit = p->mWorldRay.Intersect(Sphere(center, radius), t);
			bool hitLast = lp->mWorldRay.Intersect(Sphere(center, radius), lt);

			if (!p->mPrimaryButton) {
				mHotControl.erase(p->mName);
				continue;
			}
			if ((mHotControl.count(p->mName) == 0 || mHotControl.at(p->mName) != controlId) && (!hit || !hitLast || t.x < 0 || lt.x < 0)) continue;
			mHotControl[p->mName] = controlId;

			if (!hit) t.x = p->mWorldRay.Intersect(normalize(p->mWorldRay.mOrigin - center), center);
			if (!hitLast) lt.x = lp->mWorldRay.Intersect(normalize(lp->mWorldRay.mOrigin - center), center);

			float3 v = p->mWorldRay.mOrigin - center + p->mWorldRay.mDirection * t.x;
			float3 u = lp->mWorldRay.mOrigin - center + lp->mWorldRay.mDirection * lt.x;

			float3 rotAxis = cross(normalize(v), normalize(u));
			float angle = length(rotAxis);
			if (fabsf(angle) > .0001f)
				rotation = quaternion(asinf(angle) * sensitivity, rotAxis / angle) * rotation;

			ret = true;
	}
	return ret;
}


void GUI::DrawString(Font* font, const string& str, const float4& color, const float2& screenPos, float scale, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, float z, const fRect2D& clipRect) {
	if (str.length() == 0) return;
	GuiString s;
	s.mFont = font;
	s.mString = str;
	s.mColor = color;
	s.mOffset = screenPos;
	s.mScale = scale;
	s.mVerticalAnchor = verticalAnchor;
	s.mHorizontalAnchor = horizontalAnchor;
	s.mBounds = clipRect;
	s.mDepth = z;
	mScreenStrings.push_back(s);
}
void GUI::DrawString(Font* font, const string& str, const float4& color, const float4x4& objectToWorld, const float2& offset, float scale, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect) {
	if (str.length() == 0) return;
	GuiString s;
	s.mTransform = objectToWorld;
	s.mFont = font;
	s.mString = str;
	s.mColor = color;
	s.mOffset = offset;
	s.mScale = scale;
	s.mVerticalAnchor = verticalAnchor;
	s.mHorizontalAnchor = horizontalAnchor;
	s.mBounds = clipRect;
	mWorldStrings.push_back(s);
}

void GUI::Rect(const fRect2D& screenRect, const float4& color, Texture* texture, const float4& textureST, float z, const fRect2D& clipRect) {
	if (!clipRect.Intersects(screenRect)) return;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		float2 c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		if (screenRect.Contains(c) && clipRect.Contains(c)) i->mMousePointer.mGuiHitT = 0.f;
	}

	GuiRect r = {};
	r.ScaleTranslate = float4(screenRect.mExtent, screenRect.mOffset);
	r.Color = color;
	r.Bounds = clipRect;
	r.Depth = z;
	r.TextureST = textureST;

	if (texture) {
		if (mTextureMap.count(texture))
			r.TextureIndex = mTextureMap.at(texture);
		else {
			r.TextureIndex = mTextureArray.size();
			mTextureMap.emplace(texture, mTextureArray.size());
			mTextureArray.push_back(texture);
		}
		mScreenTextureRects.push_back(r);
	} else
		mScreenRects.push_back(r);
}
void GUI::Rect(const float4x4& transform, const fRect2D& rect, const float4& color, Texture* texture, const float4& textureST, const fRect2D& clipRect) {
	if (!clipRect.Intersects(rect)) return;

	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices()) {
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;
			if (rect.Contains(c) && clipRect.Contains(c)) const_cast<InputPointer*>(p)->mGuiHitT = t;
		}
	}


	GuiRect r = {};
	r.ObjectToWorld = transform;
	r.ScaleTranslate = float4(rect.mExtent, rect.mOffset);
	r.Color = color;
	r.Bounds = clipRect;
	r.TextureST = textureST;

	if (texture) {
		if (mTextureMap.count(texture))
			r.TextureIndex = mTextureMap.at(texture);
		else {
			r.TextureIndex = mTextureArray.size();
			mTextureMap.emplace(texture, mTextureArray.size());
			mTextureArray.push_back(texture);
		}
		mWorldTextureRects.push_back(r);
	} else
		mWorldRects.push_back(r);
}

void GUI::Label(Font* font, const string& text, float textScale, const fRect2D& screenRect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, float z, const fRect2D& clipRect){
	if (!clipRect.Intersects(screenRect)) return;
	if (color.a > 0) Rect(screenRect, color, nullptr, 0, z, clipRect);
	if (textColor.a > 0 && text.length()){
		float2 o = 0;
		if (horizontalAnchor == TEXT_ANCHOR_MID) o.x = screenRect.mExtent.x * .5f;
		if (horizontalAnchor == TEXT_ANCHOR_MAX) o.x = screenRect.mExtent.x;
		if (verticalAnchor == TEXT_ANCHOR_MID) o.y = screenRect.mExtent.y * .5f;
		if (verticalAnchor == TEXT_ANCHOR_MAX) o.y = screenRect.mExtent.y;
		DrawString(font, text, textColor, screenRect.mOffset + o, textScale, horizontalAnchor, verticalAnchor, z + DEPTH_DELTA, clipRect);
	}
}
void GUI::Label(Font* font, const string& text, float textScale, const float4x4& transform, const fRect2D& rect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect){
	if (!clipRect.Intersects(rect)) return;
	if (color.a > 0) Rect(transform,rect, color, nullptr, 0, clipRect);
	if (textColor.a > 0 && text.length()){
		float2 o = 0;
		if (horizontalAnchor == TEXT_ANCHOR_MID) o.x = rect.mExtent.x * .5f;
		if (horizontalAnchor == TEXT_ANCHOR_MAX) o.x = rect.mExtent.x;
		if (verticalAnchor == TEXT_ANCHOR_MID) o.y = rect.mExtent.y * .5f;
		if (verticalAnchor == TEXT_ANCHOR_MAX) o.y = rect.mExtent.y;
		DrawString(font, text, textColor, transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), rect.mOffset + o, textScale, horizontalAnchor, verticalAnchor, clipRect);
	}
}

bool GUI::TextButton(Font* font, const string& text, float textScale, const fRect2D& screenRect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, float z, const fRect2D& clipRect){
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(screenRect)) return false;

	bool hover = false;
	bool click = false;
	bool ret = false;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		float2 c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		const InputPointer* p = i->GetPointer(0);

		hover = screenRect.Contains(c) && clipRect.Contains(c);
		click = p->mPrimaryButton && (hover || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

		if (hover || click) i->mMousePointer.mGuiHitT = 0.f;
		if (click) mHotControl[p->mName] = controlId;
		ret = hover && (p->mPrimaryButton && !i->GetPointerLast(0)->mPrimaryButton);
	}

	fRect2D r = screenRect;
	float m = 1.f;
	if (hover) m = 1.2f;
	if (click) { m = 0.8f; r.mOffset += float2(1, -1); }

	if (color.a > 0)
		Rect(r, float4(color.rgb * m, color.a), nullptr, 0, z, clipRect);

	if (textColor.a > 0 && text.length()){
		float2 o = 0;
		if (horizontalAnchor == TEXT_ANCHOR_MID) o.x = r.mExtent.x * .5f;
		else if (horizontalAnchor == TEXT_ANCHOR_MAX) o.x = r.mExtent.x - 4;
		else o.x = 4;
		if (verticalAnchor == TEXT_ANCHOR_MID) o.y = r.mExtent.y * .5f;
		else if (verticalAnchor == TEXT_ANCHOR_MAX) o.y = r.mExtent.y - 2;
		else o.y = 2;
		DrawString(font, text, float4(textColor.rgb * m, textColor.a), r.mOffset + o, textScale, horizontalAnchor, verticalAnchor, z + DEPTH_DELTA, clipRect);
	}
	return ret;
}
bool GUI::TextButton(Font* font, const string& text, float textScale, const float4x4& transform, const fRect2D& rect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect){
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(rect)) return false;

	bool hover = false;
	bool click = false;
	bool first = false;

	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices()) {
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);
			
			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;

			bool hvr = rect.Contains(c) && clipRect.Contains(c);
			bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

			if (hvr || clk) {
				hover = true;
				const_cast<InputPointer*>(p)->mGuiHitT = t;
			}
			if (clk) {
				click = true;
				mHotControl[p->mName] = controlId;
				if (p->mPrimaryButton && !device->GetPointerLast(i)->mPrimaryButton) first = true;
			}
		}
	}


	fRect2D r = rect;
	float m = 1.f;
	if (hover) m = 1.2f;
	if (click) { m = 0.8f; r.mOffset += float2(1, -1); }

	if (color.a > 0)
		Rect(transform, r, float4(color.rgb * m, color.a), nullptr, 0, clipRect);
	
	if (textColor.a > 0 && text.length()) {
		float2 o = 0;
		if (horizontalAnchor == TEXT_ANCHOR_MID) o.x = r.mExtent.x * .5f;
		else if (horizontalAnchor == TEXT_ANCHOR_MAX) o.x = r.mExtent.x - 4;
		else o.x = 4;
		if (verticalAnchor == TEXT_ANCHOR_MID) o.y = r.mExtent.y * .5f;
		else if (verticalAnchor == TEXT_ANCHOR_MAX) o.y = r.mExtent.y - 2;
		else o.y = 2;
		DrawString(font, text, float4(textColor.rgb * m, textColor.a), transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), r.mOffset + o, textScale, horizontalAnchor, verticalAnchor, clipRect);
	}
	return hover && first;
}

bool GUI::ImageButton(const fRect2D& screenRect, const float4& color, Texture* texture, const float4& textureST, float z, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(screenRect)) return false;

	bool hover = false;
	bool click = false;
	bool ret = false;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		float2 c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		const InputPointer* p = i->GetPointer(0);

		hover = screenRect.Contains(c) && clipRect.Contains(c);
		click = p->mPrimaryButton && (hover || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

		if (hover || click) i->mMousePointer.mGuiHitT = 0.f;
		if (click) mHotControl[p->mName] = controlId;
		ret = hover && (p->mPrimaryButton && !i->GetPointerLast(0)->mPrimaryButton);
	}

	if (color.a > 0) {
		fRect2D r = screenRect;
		float m = 1.f;
		if (hover) m = 1.2f;
		if (click) { m = 1.5f; r.mOffset += float2(1, -1); }
		Rect(r, float4(color.rgb * m, color.a), texture, textureST, z, clipRect);
	}
	return ret;
}
bool GUI::ImageButton(const float4x4& transform, const fRect2D& rect, const float4& color, Texture* texture, const float4& textureST, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(rect)) return false;

	bool hover = false;
	bool click = false;
	bool first = false;

	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices()) {
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;

			bool hvr = rect.Contains(c) && clipRect.Contains(c);
			bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

			if (hvr || clk) {
				hover = true;
				const_cast<InputPointer*>(p)->mGuiHitT = t;
			}
			if (clk) {
				click = true;
				mHotControl[p->mName] = controlId;
				if (p->mPrimaryButton && !device->GetPointerLast(i)->mPrimaryButton) first = true;
			}
		}
	}

	if (color.a > 0) {
		fRect2D r = rect;
		float m = 1.f;
		if (hover) m = 1.2f;
		if (click) { m = 1.5f; r.mOffset += float2(1, -1); }
		Rect(transform, r, float4(color.rgb * m, color.a), texture, textureST, clipRect);
	}
	return hover && first;
}

bool GUI::Slider(float& value, float minimum, float maximum, LayoutAxis axis, float knobSize, const fRect2D& screenRect, const float4& barColor, const float4& knobColor, float z, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(screenRect)) return false;

	uint32_t scrollAxis = axis == LAYOUT_HORIZONTAL ? 0 : 1;
	uint32_t otherAxis = axis == LAYOUT_HORIZONTAL ? 1 : 0;

	bool hover = false;
	bool click = false;
	bool ret = false;

	fRect2D barRect = screenRect;
	fRect2D interactRect = screenRect;
	if (knobSize > interactRect.mExtent[otherAxis]) {
		interactRect.mOffset[otherAxis] += interactRect.mExtent[otherAxis] * .5f - knobSize * .5f;
		interactRect.mExtent[otherAxis] = knobSize;
	}

	// Modify position from input
	MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>();
	float2 cursor = i->CursorPos();
	float2 lastCursor = i->LastCursorPos();
	cursor.y = i->WindowHeight() - cursor.y;
	lastCursor.y = i->WindowHeight() - lastCursor.y;
	const InputPointer* p = i->GetPointer(0);
	if ((i->KeyDown(MOUSE_LEFT) && mLastHotControl[p->mName] == controlId) || (i->KeyDownFirst(MOUSE_LEFT) && interactRect.Contains(cursor)) ) {
		value = minimum + (cursor[scrollAxis] - barRect.mOffset[scrollAxis]) / barRect.mExtent[scrollAxis] * (maximum - minimum);
		ret = true;
		hover = true;
		click = true;
	}

	// Derive modified value from modified position
	value = clamp(value, minimum, maximum);
	
	fRect2D knobRect(barRect.mOffset, knobSize);
	knobRect.mOffset[scrollAxis] += (value - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	knobRect.mOffset[otherAxis] += barRect.mExtent[otherAxis] *.5f;
	knobRect.mOffset -= knobSize*.5f;

	bool hvr = (interactRect.Contains(cursor)) && clipRect.Contains(cursor);

	if (hover || click) i->mMousePointer.mGuiHitT = 0.f;
	if (click) mHotControl[p->mName] = controlId;

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(barRect, barColor, nullptr, 0, z, clipRect);
	Rect(knobRect, float4(knobColor.rgb * m, knobColor.a), mIconsTexture, ICON_CIRCLE_ST, z + DEPTH_DELTA, clipRect);

	return ret;
}
bool GUI::Slider(float& value, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4x4& transform, const fRect2D& rect, const float4& barColor, const float4& knobColor, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(rect)) return false;

	bool ret = false;

	fRect2D barRect = rect;

	uint32_t scrollAxis = axis == LAYOUT_HORIZONTAL ? 0 : 1;
	uint32_t otherAxis = axis == LAYOUT_HORIZONTAL ? 1 : 0;

	// Determine knob position
	float knobPos = barRect.mOffset[scrollAxis] + (value - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];

	// Modify kob position from input
	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices())
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;
			if (rect.Contains(c) && clipRect.Contains(c))
				knobPos += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .25f;

			if (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId) {
				Ray ray = p->mWorldRay;
				Ray lastRay = device->GetPointerLast(i)->mWorldRay;
				ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
				ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;
				lastRay.mOrigin = (invTransform * float4(lastRay.mOrigin, 1)).xyz;
				lastRay.mDirection = (invTransform * float4(lastRay.mDirection, 0)).xyz;
				float c = (ray.mOrigin + ray.mDirection * ray.Intersect(float4(0, 0, 1, 0)))[scrollAxis];
				float lc = (lastRay.mOrigin + lastRay.mDirection * lastRay.Intersect(float4(0, 0, 1, 0)))[scrollAxis];
				knobPos += c - lc;
				ret = true;
			}
		}

	// Derive modified value from modified position
	value = minimum + (knobPos - barRect.mOffset[scrollAxis]) / barRect.mExtent[scrollAxis] * (maximum - minimum);
	value = clamp(value, minimum, maximum);
	knobPos = barRect.mOffset[scrollAxis] + (value - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];

	fRect2D knobRect(barRect.mOffset, knobSize);
	knobRect.mOffset[scrollAxis] += knobPos;
	knobRect.mOffset -= knobSize*.5f;

	bool hover = false;
	bool click = false;

	for (InputDevice* device : mInputManager->InputDevices())
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;
			bool hvr = knobRect.Contains(c) && clipRect.Contains(c);
			bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

			if (hvr || clk) {
				hover = true;
				const_cast<InputPointer*>(p)->mGuiHitT = t;
			}
			if (clk) {
				mHotControl[p->mName] = controlId;
				click = true;
			}
		}

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(transform, barRect, barColor, nullptr, 0, clipRect);
	Rect(transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), knobRect, float4(knobColor.rgb * m, knobColor.a), nullptr, 0, clipRect);

	return ret;
}

bool GUI::RangeSlider(float2& values, float minimum, float maximum, LayoutAxis axis, float knobSize, const fRect2D& screenRect, const float4& barColor, const float4& knobColor, float z, const fRect2D& clipRect) {
	uint32_t controlId[2];
	controlId[0] = mNextControlId++;
	controlId[1] = mNextControlId++;
	if (!clipRect.Intersects(screenRect)) return false;

	uint32_t scrollAxis = axis == LAYOUT_HORIZONTAL ? 0 : 1;
	uint32_t otherAxis = axis == LAYOUT_HORIZONTAL ? 1 : 0;

	bool ret = false;
	bool hover = false;
	bool click = false;

	fRect2D barRect = screenRect;

	MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>();
	float2 cursor = i->CursorPos();
	const InputPointer* p = i->GetPointer(0);
	float2 lastCursor = i->CursorPos();
	// screen space -> UI space
	cursor.y = i->WindowHeight() - cursor.y;
	lastCursor.y = i->WindowHeight() - lastCursor.y;

	for (uint32_t j = 0; j < 2; j++)
		if (i->KeyDown(MOUSE_LEFT) && mLastHotControl[p->mName] == controlId[j]) {
				values[j] = minimum + (cursor[scrollAxis] - barRect.mOffset[scrollAxis]) / barRect.mExtent[scrollAxis] * (maximum - minimum);
				mHotControl[p->mName] = controlId[j];
				const_cast<InputPointer*>(p)->mGuiHitT = 0.f;
				ret = true;
				hover = true;
				click = true;
			}

	values = clamp(values, minimum, maximum);

	fRect2D knobRects[2];
	knobRects[0] = fRect2D(barRect.mOffset, knobSize);
	knobRects[1] = fRect2D(barRect.mOffset, knobSize);
	knobRects[0].mOffset[scrollAxis] += (values[0] - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	knobRects[1].mOffset[scrollAxis] += (values[1] - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	knobRects[0].mOffset[otherAxis] += barRect.mExtent[otherAxis] * .5f;
	knobRects[1].mOffset[otherAxis] += barRect.mExtent[otherAxis] * .5f;
	knobRects[0].mOffset -= knobSize*.5f;
	knobRects[1].mOffset -= knobSize*.5f;
	
	if (!hover) {
		for (uint32_t j = 0; j < 2; j++){
			if (knobRects[j].Contains(cursor)) {
				hover = true;
				if (i->KeyDownFirst(MOUSE_LEFT)) {
					click = true;
					mHotControl[p->mName] = controlId[j];
				}
			}
		}
	}

	fRect2D middleRect = barRect;
	middleRect.mOffset[scrollAxis] = min(knobRects[0].mOffset[scrollAxis], knobRects[1].mOffset[scrollAxis]) + knobSize * .5f;
	middleRect.mExtent[scrollAxis] = fabsf(knobRects[1].mOffset[scrollAxis] - knobRects[0].mOffset[scrollAxis]);

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(barRect, barColor, nullptr, 0, z, clipRect);
	Rect(knobRects[0], float4(knobColor.rgb * m, knobColor.a), mIconsTexture, ICON_TRI_RIGHT_ST, z, clipRect);
	Rect(knobRects[1], float4(knobColor.rgb * m, knobColor.a), mIconsTexture, ICON_TRI_LEFT_ST, z, clipRect);
	if (middleRect.mExtent[scrollAxis] > 0)
		Rect(middleRect, float4(knobColor.rgb * m, knobColor.a), nullptr, 0, z, clipRect);
	return ret;
}
bool GUI::RangeSlider(float2& valueRange, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4x4& transform, const fRect2D& rect, const float4& barColor, const float4& knobColor, const fRect2D& clipRect) {
	uint32_t controlIds[3] = { mNextControlId++, mNextControlId++, mNextControlId++ };
	if (!clipRect.Intersects(rect)) return false;

	bool ret = false;

	fRect2D barRect = rect;

	uint32_t scrollAxis = axis == LAYOUT_HORIZONTAL ? 0 : 1;
	uint32_t otherAxis = axis == LAYOUT_HORIZONTAL ? 1 : 0;

	// Determine knob positions
	float2 pos = barRect.mOffset[scrollAxis] + (valueRange - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];

	// Modify kob positions from input
	float4x4 invTransform = inverse(transform);
	for (InputDevice* device : mInputManager->InputDevices())
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;
			if (rect.Contains(c) && clipRect.Contains(c)) {
				if (c.x < pos[0] + knobSize * .5f)
					pos[0] += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .25f;
				else if (c.x > pos[1] - knobSize * .5f)
					pos[1] += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .25f;
				else
					pos += p->mScrollDelta[scrollAxis] * barRect.mExtent[scrollAxis] * .25f;
			}

			for (uint32_t j = 0; j < 3; j++)
				if (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlIds[j]) {
					Ray ray = p->mWorldRay;
					Ray lastRay = device->GetPointerLast(i)->mWorldRay;
					ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
					ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;
					lastRay.mOrigin = (invTransform * float4(lastRay.mOrigin, 1)).xyz;
					lastRay.mDirection = (invTransform * float4(lastRay.mDirection, 0)).xyz;
					float c = (ray.mOrigin + ray.mDirection * ray.Intersect(float4(0, 0, 1, 0)))[scrollAxis];
					float lc = (lastRay.mOrigin + lastRay.mDirection * lastRay.Intersect(float4(0, 0, 1, 0)))[scrollAxis];
					if (j == 2) pos += c - lc;
					else pos[j] += c - lc;
					ret = true;
				}
		}

	// Derive modified value from modified positions
	valueRange = minimum + (pos - barRect.mOffset[scrollAxis]) / barRect.mExtent[scrollAxis] * (maximum - minimum);
	valueRange = clamp(valueRange, minimum, maximum);
	if (valueRange.x > valueRange.y) swap(valueRange.x, valueRange.y);

	// Derive final knob position from the modified, clamped value
	pos[0] = barRect.mOffset[scrollAxis] + (valueRange[0] - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];
	pos[1] = barRect.mOffset[scrollAxis] + (valueRange[1] - minimum) / (maximum - minimum) * barRect.mExtent[scrollAxis];

	fRect2D middleRect = rect;
	middleRect.mOffset[otherAxis] += rect.mExtent[otherAxis] * .125f;
	middleRect.mExtent[otherAxis] *= 0.75f;
	middleRect.mOffset[scrollAxis] = pos[0] + knobSize;
	middleRect.mExtent[scrollAxis] = pos[1] - (pos[0] + knobSize);

	fRect2D knobRects[2];
	knobRects[0] = fRect2D(barRect.mOffset, knobSize);
	knobRects[1] = fRect2D(barRect.mOffset, knobSize);
	knobRects[0].mOffset[scrollAxis] += pos[0];
	knobRects[1].mOffset[scrollAxis] += pos[1];
	knobRects[0].mOffset -= knobSize*.5f;
	knobRects[1].mOffset -= knobSize*.5f;
	
	bool hover = false;
	bool click = false;

	for (InputDevice* device : mInputManager->InputDevices())
		for (uint32_t i = 0; i < device->PointerCount(); i++) {
			const InputPointer* p = device->GetPointer(i);

			Ray ray = p->mWorldRay;
			ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
			ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

			float t = ray.Intersect(float4(0, 0, 1, 0));
			if (p->mGuiHitT > 0 && t > p->mGuiHitT) continue;

			float2 c = (ray.mOrigin + ray.mDirection * t).xy;

			for (uint32_t j = 0; j < 2; j++) {
				bool hvr = knobRects[j].Contains(c) && clipRect.Contains(c);
				bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlIds[j]));

				if (hvr || clk) {
					hover = true;
					const_cast<InputPointer*>(p)->mGuiHitT = t;
				}
				if (clk) {
					mHotControl[p->mName] = controlIds[j];
					click = true;
				}
			}

			if (middleRect.mExtent[scrollAxis] > 0) {
				bool hvr = middleRect.Contains(c) && clipRect.Contains(c);
				bool clk = p->mPrimaryButton && (hvr || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlIds[2]));

				if (hvr || clk) {
					hover = true;
					const_cast<InputPointer*>(p)->mGuiHitT = t;
				}
				if (clk) {
					mHotControl[p->mName] = controlIds[2];
					click = true;
				}
			}
		}

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(transform, barRect, barColor, nullptr, 0, clipRect);
	Rect(transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), knobRects[0], float4(knobColor.rgb * m, knobColor.a), nullptr, 0, clipRect);
	Rect(transform* float4x4::Translate(float3(0, 0, DEPTH_DELTA)), knobRects[1], float4(knobColor.rgb* m, knobColor.a), nullptr, 0, clipRect);
	if (middleRect.mExtent[scrollAxis] > 0)
		Rect(transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), middleRect, float4(knobColor.rgb * m, knobColor.a), nullptr, 0, clipRect);
	return ret;
}


fRect2D GUI::GuiLayout::Get(float size) {
	fRect2D layoutRect = mRect;
	switch (mAxis) {
	case LAYOUT_VERTICAL:
		layoutRect.mExtent.y = size;
		layoutRect.mOffset.y += mRect.mExtent.y - (mLayoutPosition + size);
		break;
	case LAYOUT_HORIZONTAL:
		layoutRect.mOffset.x += mLayoutPosition;
		layoutRect.mExtent.x = size;
		break;
	}
	mLayoutPosition += size;
	return layoutRect;
}

fRect2D GUI::BeginScreenLayout(LayoutAxis axis, const fRect2D& screenRect) {
	fRect2D layoutRect(screenRect.mOffset + mLayoutTheme.mControlPadding, screenRect.mExtent - mLayoutTheme.mControlPadding * 2);
	mLayoutStack.push({ float4x4(1), true, axis, layoutRect, layoutRect, 0, START_DEPTH + DEPTH_DELTA });
	if (mLayoutTheme.mBackgroundColor.a > 0) Rect(screenRect, mLayoutTheme.mBackgroundColor, nullptr, 0, START_DEPTH);
	return layoutRect;
}
fRect2D GUI::BeginWorldLayout(LayoutAxis axis, const float4x4& tranform, const fRect2D& rect) {
	fRect2D layoutRect(rect.mOffset + mLayoutTheme.mControlPadding, rect.mExtent - mLayoutTheme.mControlPadding * 2);
	mLayoutStack.push({ tranform, false, axis, layoutRect, layoutRect, 0, START_DEPTH + DEPTH_DELTA });
	if (mLayoutTheme.mBackgroundColor.a > 0) Rect(tranform * float4x4::Translate(float3(0, 0, START_DEPTH)), rect, mLayoutTheme.mBackgroundColor);
	return layoutRect;
}

fRect2D GUI::BeginSubLayout(LayoutAxis axis, float size) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(size);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (mLayoutTheme.mBackgroundColor.a > 0) {
		if (l.mScreenSpace)
			Rect(layoutRect, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mLayoutDepth + DEPTH_DELTA, l.mClipRect);
		else
			Rect(l.mTransform + float4x4::Translate(float3(0, 0, l.mLayoutDepth + DEPTH_DELTA)), layoutRect, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mClipRect);
	}

	layoutRect.mOffset += mLayoutTheme.mControlPadding;
	layoutRect.mExtent -= mLayoutTheme.mControlPadding * 2;

	mLayoutStack.push({ l.mTransform, l.mScreenSpace, axis, layoutRect, l.mClipRect, 0, l.mLayoutDepth + DEPTH_DELTA });

	return layoutRect;
}
fRect2D GUI::BeginScrollSubLayout(float size, float contentSize) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(size);
	LayoutSpace(mLayoutTheme.mControlPadding);

	uint32_t controlId = mNextControlId++;

	float scrollAmount = 0;
	if (mControlData.count(controlId)) {
		const auto& v = mControlData.at(controlId);
		if (v.index() == 0) scrollAmount = get<float>(v);
	}

	if (l.mScreenSpace) {
		if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
			float2 c = i->CursorPos();
			c.y = i->WindowHeight() - c.y;
			if (layoutRect.Contains(c) && l.mClipRect.Contains(c)) {
				scrollAmount -= i->ScrollDelta() * 60;
				i->mMousePointer.mGuiHitT = 0.f;
			}
		}
	} else {
		float4x4 invTransform = inverse(l.mTransform);
		for (const InputDevice* d : mInputManager->InputDevices())
			for (uint32_t i = 0; i < d->PointerCount(); i++) {
				const InputPointer* p = d->GetPointer(i);

				Ray ray = p->mWorldRay;
				ray.mOrigin = (invTransform * float4(ray.mOrigin, 1)).xyz;
				ray.mDirection = (invTransform * float4(ray.mDirection, 0)).xyz;

				float t = ray.Intersect(float4(0, 0, 1, l.mLayoutDepth));

				float2 c = (ray.mOrigin + ray.mDirection * t).xy;
				if (layoutRect.Contains(c) && l.mClipRect.Contains(c)) {
					scrollAmount -= p->mScrollDelta[l.mAxis == LAYOUT_HORIZONTAL ? 0 : 1] * contentSize * .25f;
					const_cast<InputPointer*>(p)->mGuiHitT = t;
				}
			}
	}

	float scrollMax = max(0.f, contentSize - layoutRect.mExtent.y);
	scrollAmount = clamp(scrollAmount, 0.f, scrollMax);

	mControlData[controlId] = scrollAmount;

	if (mLayoutTheme.mBackgroundColor.a > 0) {
		if (l.mScreenSpace)
			Rect(layoutRect, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mLayoutDepth, l.mClipRect);
		else
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mClipRect);
	}

	fRect2D contentRect = layoutRect;
	contentRect.mOffset += mLayoutTheme.mControlPadding;
	contentRect.mExtent -= mLayoutTheme.mControlPadding * 2;
	switch (l.mAxis) {
	case LAYOUT_HORIZONTAL:
		contentRect.mOffset.x -= scrollAmount + (layoutRect.mExtent.x - contentSize);
		contentRect.mExtent.x = contentSize - mLayoutTheme.mControlPadding * 2;
		break;
	case LAYOUT_VERTICAL:
		contentRect.mOffset.y += (layoutRect.mExtent.y - contentSize) + scrollAmount;
		contentRect.mExtent.y = contentSize - mLayoutTheme.mControlPadding * 2;
		break;
	}
	
	// scroll bar slider
	if (scrollMax > 0) {
		fRect2D slider;
		fRect2D sliderbg;

		switch (l.mAxis) {
		case LAYOUT_HORIZONTAL:
			slider.mExtent = float2(layoutRect.mExtent.x * (layoutRect.mExtent.x / contentSize), mLayoutTheme.mScrollBarThickness);
			slider.mOffset = layoutRect.mOffset + float2((layoutRect.mExtent.x - slider.mExtent.x) * (scrollAmount / scrollMax), 0);
			sliderbg.mOffset = layoutRect.mOffset;
			sliderbg.mExtent = float2(layoutRect.mExtent.x, slider.mExtent.y);

			layoutRect.mOffset.y += slider.mExtent.y;
			layoutRect.mExtent.y -= slider.mExtent.y;
			layoutRect.mOffset.y += slider.mExtent.y;
			layoutRect.mExtent.y -= slider.mExtent.y;
			break;

		case LAYOUT_VERTICAL:
			slider.mExtent = float2(mLayoutTheme.mScrollBarThickness, layoutRect.mExtent.y * (layoutRect.mExtent.y / contentSize));
			slider.mOffset = layoutRect.mOffset + float2(layoutRect.mExtent.x - slider.mExtent.x, (layoutRect.mExtent.y - slider.mExtent.y) * (1 - scrollAmount / scrollMax));
			sliderbg.mOffset = layoutRect.mOffset + float2(layoutRect.mExtent.x - slider.mExtent.x, 0);
			sliderbg.mExtent = float2(slider.mExtent.x, layoutRect.mExtent.y);

			layoutRect.mExtent.x -= slider.mExtent.x;
			break;
		}

		uint32_t scrollAxis = l.mAxis == LAYOUT_HORIZONTAL ? 0 : 1;
		uint32_t otherAxis = l.mAxis == LAYOUT_HORIZONTAL ? 1 : 0;

		float2 offset = slider.mOffset;
		float extent = slider.mExtent[otherAxis];
		slider.mOffset[scrollAxis] += extent / 2;
		slider.mExtent[scrollAxis] = fmaxf(0, slider.mExtent[scrollAxis] - extent);
		
		if (l.mScreenSpace) {
			Rect(sliderbg, mLayoutTheme.mSliderColor, nullptr, 0, l.mLayoutDepth + DEPTH_DELTA);
			Rect(slider, mLayoutTheme.mSliderKnobColor, nullptr, 0, l.mLayoutDepth + 2*DEPTH_DELTA);
			
			Rect(fRect2D(offset, extent), mLayoutTheme.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mLayoutDepth + 2*DEPTH_DELTA, l.mClipRect);
			offset[scrollAxis] += floorf(slider.mExtent[scrollAxis] - 0.5f);
			Rect(fRect2D(offset, extent), mLayoutTheme.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mLayoutDepth + 2*DEPTH_DELTA, l.mClipRect);
		} else {
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + DEPTH_DELTA)), sliderbg, mLayoutTheme.mSliderColor);
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), slider, mLayoutTheme.mSliderKnobColor);
			
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), fRect2D(offset, extent), mLayoutTheme.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
			offset[scrollAxis] += slider.mExtent[scrollAxis];
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), fRect2D(offset, extent), mLayoutTheme.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
		}
	}

	mLayoutStack.push({ l.mTransform, l.mScreenSpace, l.mAxis, contentRect, layoutRect, 0, l.mLayoutDepth + 3*DEPTH_DELTA });

	return contentRect;
}
void GUI::EndLayout() {
	mLayoutStack.pop();
}

void GUI::LayoutSpace(float size) {
	mLayoutStack.top().mLayoutPosition += size;
}
void GUI::LayoutSeparator() {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(2);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace)
		GUI::Rect(layoutRect, mLayoutTheme.mTextColor, nullptr, 0, l.mLayoutDepth, l.mClipRect);
	else
		GUI::Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mTextColor, nullptr, 0, l.mClipRect);
}
void GUI::LayoutTitle(const string& text, TextAnchor horizontalAnchor, TextAnchor verticalAnchor) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mTitleFont->CharacterHeight());
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace)
		Label(mLayoutTheme.mTitleFont, text, 1, layoutRect, 0, mLayoutTheme.mTextColor, horizontalAnchor, verticalAnchor, l.mLayoutDepth, l.mClipRect);
	else
		Label(mLayoutTheme.mTitleFont, text, 1, l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, 0, mLayoutTheme.mTextColor, horizontalAnchor, verticalAnchor, l.mClipRect);
}
void GUI::LayoutLabel(const string& text, TextAnchor horizontalAnchor, TextAnchor verticalAnchor) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace)
		Label(mLayoutTheme.mControlFont, text, 1, layoutRect, 0, mLayoutTheme.mTextColor, horizontalAnchor, verticalAnchor, l.mLayoutDepth, l.mClipRect);
	else
		Label(mLayoutTheme.mControlFont, text, 1, l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, 0, mLayoutTheme.mTextColor, horizontalAnchor, verticalAnchor, l.mClipRect);
}
bool GUI::LayoutTextButton(const string& text, TextAnchor horizontalAnchor, TextAnchor verticalAnchor) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace)
		return TextButton(mLayoutTheme.mControlFont, text, 1, layoutRect, mLayoutTheme.mControlBackgroundColor, mLayoutTheme.mTextColor, horizontalAnchor, verticalAnchor, l.mLayoutDepth, l.mClipRect);
	else
		return TextButton(mLayoutTheme.mControlFont, text, 1, l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mControlBackgroundColor, mLayoutTheme.mTextColor, horizontalAnchor, verticalAnchor, l.mClipRect);
}
bool GUI::LayoutImageButton(const float2 size, Texture* texture, const float4& textureST) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(size[l.mAxis]);
	LayoutSpace(mLayoutTheme.mControlPadding);

	layoutRect.mExtent = size;

	if (l.mScreenSpace)
		return ImageButton(layoutRect, mLayoutTheme.mTextColor, texture, textureST, l.mLayoutDepth, l.mClipRect);
	else
		return ImageButton(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mTextColor, texture, textureST, l.mClipRect);
}
bool GUI::LayoutToggle(const string& label, bool& value) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace) {
		Label(mLayoutTheme.mControlFont, label, 1, layoutRect, 0, mLayoutTheme.mTextColor, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MID, l.mLayoutDepth, l.mClipRect);
		layoutRect.mOffset.x += layoutRect.mExtent.x - mLayoutTheme.mControlSize;
		layoutRect.mExtent = mLayoutTheme.mControlSize;
		if (ImageButton(layoutRect, mLayoutTheme.mTextColor, mIconsTexture, value ? ICON_CHECK_ST : ICON_CHECKBOX_ST, l.mLayoutDepth, l.mClipRect)) {
			value = !value;
			return true;
		}
		return false;
	} else {
		Label(mLayoutTheme.mControlFont, label, 1, l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, 0, mLayoutTheme.mTextColor, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MID, l.mClipRect);
		layoutRect.mOffset.x += layoutRect.mExtent.x - mLayoutTheme.mControlSize;
		layoutRect.mExtent = mLayoutTheme.mControlSize;
		if (ImageButton(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mTextColor, mIconsTexture, value ? ICON_CHECK_ST : ICON_CHECKBOX_ST, l.mClipRect)) {
			value = !value;
			return true;
		}
		return false;
	}
}
bool GUI::LayoutSlider(const string& label, float& value, float minimum, float maximum) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	// make the bar the right size
	layoutRect.mOffset[l.mAxis] += (layoutRect.mExtent[l.mAxis] - mLayoutTheme.mSliderBarSize) * .5f;
	layoutRect.mExtent[l.mAxis] = mLayoutTheme.mSliderBarSize;

	LayoutAxis axis = l.mAxis == LAYOUT_HORIZONTAL ? LAYOUT_VERTICAL : LAYOUT_HORIZONTAL;
	if (l.mScreenSpace) {
		fRect2D labelRect = layoutRect;
		labelRect.mExtent.x *= .25f;
		Label(mLayoutTheme.mControlFont, label, 1, labelRect, 0, mLayoutTheme.mTextColor, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MID, l.mLayoutDepth, l.mClipRect);
		layoutRect.mOffset.x += labelRect.mExtent.x + mLayoutTheme.mSliderKnobSize*.5f;
		layoutRect.mExtent.x -= labelRect.mExtent.x + mLayoutTheme.mSliderKnobSize;
		return Slider(value, minimum, maximum, axis, mLayoutTheme.mSliderKnobSize, layoutRect, mLayoutTheme.mSliderColor, mLayoutTheme.mSliderKnobColor, l.mLayoutDepth, l.mClipRect);
	} else {
		fRect2D labelRect = layoutRect;
		labelRect.mExtent.x *= .25f;
		Label(mLayoutTheme.mControlFont, label, 1, l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), labelRect, 0, mLayoutTheme.mTextColor, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MID, l.mClipRect);
		layoutRect.mOffset.x += labelRect.mExtent.x + mLayoutTheme.mSliderKnobSize*.5f;
		layoutRect.mExtent.x -= labelRect.mExtent.x + mLayoutTheme.mSliderKnobSize;
		return Slider(value, minimum, maximum, axis, mLayoutTheme.mSliderKnobSize, l.mTransform, layoutRect, mLayoutTheme.mSliderColor, mLayoutTheme.mSliderKnobColor, l.mClipRect);
	}
}
bool GUI::LayoutRangeSlider(const string& label, float2& values, float minimum, float maximum) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	// make the bar the right size
	layoutRect.mOffset[l.mAxis] += (layoutRect.mExtent[l.mAxis] - mLayoutTheme.mSliderBarSize) * .5f;
	layoutRect.mExtent[l.mAxis] = mLayoutTheme.mSliderBarSize;

	LayoutAxis axis = l.mAxis == LAYOUT_HORIZONTAL ? LAYOUT_VERTICAL : LAYOUT_HORIZONTAL;
	if (l.mScreenSpace) {
		fRect2D labelRect = layoutRect;
		labelRect.mExtent.x *= .25f;
		Label(mLayoutTheme.mControlFont, label, 1, labelRect, 0, mLayoutTheme.mTextColor, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MID, l.mLayoutDepth, l.mClipRect);
		layoutRect.mOffset.x += labelRect.mExtent.x + mLayoutTheme.mSliderKnobSize*.5f;
		layoutRect.mExtent.x -= labelRect.mExtent.x + mLayoutTheme.mSliderKnobSize;
		return RangeSlider(values, minimum, maximum, axis, mLayoutTheme.mSliderKnobSize, layoutRect, mLayoutTheme.mSliderColor, mLayoutTheme.mSliderKnobColor, l.mLayoutDepth, l.mClipRect);
	} else {
		fRect2D labelRect = layoutRect;
		labelRect.mExtent.x *= .25f;
		Label(mLayoutTheme.mControlFont, label, 1, l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), labelRect, 0, mLayoutTheme.mTextColor, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MID, l.mClipRect);
		layoutRect.mOffset.x += labelRect.mExtent.x + mLayoutTheme.mSliderKnobSize*.5f;
		layoutRect.mExtent.x -= labelRect.mExtent.x + mLayoutTheme.mSliderKnobSize;
		return RangeSlider(values, minimum, maximum, axis, mLayoutTheme.mSliderKnobSize, l.mTransform, layoutRect, mLayoutTheme.mSliderColor, mLayoutTheme.mSliderKnobColor, l.mClipRect);
	}
}