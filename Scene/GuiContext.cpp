#include <Scene/GuiContext.hpp>
#include <Core/Pipeline.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>

using namespace std;

#define CIRCLE_VERTEX_RESOLUTION 64

#define START_DEPTH 1e-4f
#define DEPTH_DELTA -1e-6f

#define ICON_CIRCLE_ST (float4(128, 128, 256, 512) / 1024.f)
#define ICON_CHECKBOX_ST (float4(128, 128, 0, 512) / 1024.f)
#define ICON_CHECK_ST (float4(128, 128, 128, 512) / 1024.f)
#define ICON_TRI_RIGHT_ST (float4(128, 128, 384, 512) / 1024.f)
#define ICON_TRI_LEFT_ST (float4(128, 128, 512, 512) / 1024.f)

GuiContext::GuiContext(::Device* device, ::InputManager* inputManager) : mDevice(device), mInputManager(inputManager) {
	Reset();
}

void GuiContext::Reset() {
	while (mLayoutStack.size()) mLayoutStack.pop();

	mNextControlId = 10;
	mLastHotControl = mHotControl;
	mHotControl.clear();

	mTextureArray.clear();
	mTextureMap.clear();

	mWorldRects.clear();
	mWorldTextureRects.clear();
	mWorldStrings.clear();
	mWorldLines.clear();

	mScreenRects.clear();
	mScreenTextureRects.clear();
	mScreenStrings.clear();
	mScreenLines.clear();
	
	mStringGlyphs.clear();
	mStringTransforms.clear();
	mStringSDFs.clear();

	mLinePoints.clear();
	mLineTransforms.clear();

	mNextControlId = 10;

	mIconsTexture = mDevice->AssetManager()->LoadTexture("Assets/Textures/icons.png");

	mLayoutTheme.mBackgroundColor = float4(.21f, .21f, .21f, 1.f);
	mLayoutTheme.mControlBackgroundColor = float4(.16f, .16f, .16f, 1.f);
	mLayoutTheme.mTextColor = float4(0.8f, 0.8f, 0.8f, 1.f);
	mLayoutTheme.mButtonColor = float4(.31f, .31f, .31f, 1.f);
	mLayoutTheme.mSliderColor = float4(.36f, .36f, .36f, 1.f);
	mLayoutTheme.mSliderKnobColor = float4(.6f, .6f, .6f, 1.f);

	//mDevice->AssetManager()->LoadFont("Assets/Fonts/OpenSans/OpenSans-Regular.ttf"); // preload for profiler
	mLayoutTheme.mControlFont = mDevice->AssetManager()->LoadFont("Assets/Fonts/OpenSans/OpenSans-SemiBold.ttf");
	mLayoutTheme.mTitleFont = mDevice->AssetManager()->LoadFont("Assets/Fonts/OpenSans/OpenSans-Bold.ttf");
	mLayoutTheme.mControlFontHeight = 16.f;
	mLayoutTheme.mTitleFontHeight = 24.f;

	mLayoutTheme.mControlSize = 16;
	mLayoutTheme.mControlPadding = 2;
	mLayoutTheme.mSliderBarSize = 2;
	mLayoutTheme.mSliderKnobSize = 10;
	mLayoutTheme.mScrollBarThickness = 6;
}
void GuiContext::OnPreRender(CommandBuffer* commandBuffer) {
	for (Texture* t : mTextureArray)
		commandBuffer->TransitionBarrier(t, vk::ImageLayout::eShaderReadOnlyOptimal);
	for (GuiString& s : mWorldStrings) commandBuffer->TransitionBarrier(s.mFont->SDF(), vk::ImageLayout::eShaderReadOnlyOptimal);
	for (GuiString& s : mScreenStrings) commandBuffer->TransitionBarrier(s.mFont->SDF(), vk::ImageLayout::eShaderReadOnlyOptimal);
}

void GuiContext::OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) {
	float2 screenSize = float2((float)commandBuffer->CurrentFramebuffer()->Extent().width, (float)commandBuffer->CurrentFramebuffer()->Extent().height);

	Pipeline* font = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/font.stmb");
	Pipeline* ui = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/ui.stmb");

	Buffer* glyphBuffer = nullptr;
	Buffer* glyphTransforms = nullptr;
	if (mStringGlyphs.size()) {
		glyphBuffer = commandBuffer->GetBuffer("Glyphs", mStringGlyphs.size() * sizeof(GlyphRect), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
		glyphBuffer->Upload(mStringGlyphs.data(), mStringGlyphs.size() * sizeof(GlyphRect));

		if (mStringTransforms.size()) {
			glyphTransforms = commandBuffer->GetBuffer("Transforms", mStringTransforms.size() * sizeof(float4x4), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached | vk::MemoryPropertyFlagBits::eHostCoherent);
			glyphTransforms->Upload(mStringTransforms.data(), mStringTransforms.size() * sizeof(float4x4));
		}
	}

	if (mWorldRects.size()) {
		GraphicsPipeline* pipeline = ui->GetGraphics(commandBuffer->CurrentShaderPass(), {});
		if (pipeline) {
			commandBuffer->BindPipeline(pipeline);

			Buffer* screenRects = commandBuffer->GetBuffer("WorldRects", mWorldRects.size() * sizeof(GuiRect), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
			memcpy(screenRects->MappedData(), mWorldRects.data(), mWorldRects.size() * sizeof(GuiRect));

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("WorldRects", pipeline->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateBufferDescriptor("Rects", screenRects, 0, mWorldRects.size() * sizeof(GuiRect), pipeline);
			commandBuffer->BindDescriptorSet(ds, PER_OBJECT);

			camera->SetViewportScissor(commandBuffer, StereoEye::eLeft);
			((vk::CommandBuffer)*commandBuffer).draw(6, (uint32_t)mWorldRects.size(), 0, 0);
			commandBuffer->mTriangleCount += mWorldRects.size()*2;
			if (camera->StereoMode() != StereoMode::eNone) {
				camera->SetViewportScissor(commandBuffer, StereoEye::eRight);
				((vk::CommandBuffer)*commandBuffer).draw(6, (uint32_t)mWorldRects.size(), 0, 0);
				commandBuffer->mTriangleCount += mWorldRects.size()*2;
			}
		}
	}
	if (mWorldTextureRects.size()) {
		GraphicsPipeline* pipeline = ui->GetGraphics(commandBuffer->CurrentShaderPass(), { "TEXTURED" });
		if (pipeline) {
			commandBuffer->BindPipeline(pipeline);

			Buffer* screenRects = commandBuffer->GetBuffer("WorldRects", mWorldTextureRects.size() * sizeof(GuiRect), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
			memcpy(screenRects->MappedData(), mWorldTextureRects.data(), mWorldTextureRects.size() * sizeof(GuiRect));

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("WorldRects", pipeline->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateBufferDescriptor("Rects", screenRects, 0, mWorldTextureRects.size() * sizeof(GuiRect), pipeline);
			for (uint32_t i = 0; i < mTextureArray.size(); i++)
				ds->CreateTextureDescriptor("Textures", mTextureArray[i], pipeline, i);
			commandBuffer->BindDescriptorSet(ds, PER_OBJECT);

			camera->SetViewportScissor(commandBuffer, StereoEye::eLeft);
			((vk::CommandBuffer)*commandBuffer).draw(6, (uint32_t)mWorldTextureRects.size(), 0, 0);
				commandBuffer->mTriangleCount += mWorldTextureRects.size()*2;
			if (camera->StereoMode() != StereoMode::eNone) {
				camera->SetViewportScissor(commandBuffer, StereoEye::eRight);
				((vk::CommandBuffer)*commandBuffer).draw(6, (uint32_t)mWorldTextureRects.size(), 0, 0);
				commandBuffer->mTriangleCount += mWorldTextureRects.size()*2;
			}
		}
	}
	if (mWorldStrings.size() && mStringGlyphs.size() && mStringTransforms.size()) {
		GraphicsPipeline* pipeline = font->GetGraphics(commandBuffer->CurrentShaderPass(), {});
		if (pipeline) {
			commandBuffer->BindPipeline(pipeline);

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("Screen Strings DescriptorSet", pipeline->mDescriptorSetLayouts[PER_OBJECT]);
			ds->CreateBufferDescriptor("Glyphs", glyphBuffer, 0, mStringGlyphs.size() * sizeof(GlyphRect), pipeline);
			ds->CreateBufferDescriptor("Transforms", glyphTransforms, 0, mStringTransforms.size() * sizeof(float4x4), pipeline);
			for (auto s : mWorldStrings)
				ds->CreateTextureDescriptor("SDFs", s.mFont->SDF(), pipeline, s.mSdfIndex);
			commandBuffer->BindDescriptorSet(ds, PER_OBJECT);

			for (uint32_t i = 0; i < mWorldStrings.size(); i++) {
				const GuiString& s = mWorldStrings[i];
				commandBuffer->PushConstantRef("Color", s.mColor);
				commandBuffer->PushConstantRef("ClipBounds", s.mClipBounds);
				commandBuffer->PushConstantRef("SdfIndex", s.mSdfIndex);
				camera->SetViewportScissor(commandBuffer, StereoEye::eLeft);
				((vk::CommandBuffer)*commandBuffer).draw(s.mGlyphCount*6, 1, s.mGlyphIndex*6, i);
				commandBuffer->mTriangleCount += s.mGlyphCount*2;
				if (camera->StereoMode() != StereoMode::eNone) {
					camera->SetViewportScissor(commandBuffer, StereoEye::eRight);
					((vk::CommandBuffer)*commandBuffer).draw(s.mGlyphCount*6, 1, s.mGlyphIndex*6, i);
					commandBuffer->mTriangleCount += s.mGlyphCount*2;
				}
			}
		}
	}

	if (camera->StereoMode() == StereoMode::eNone) {
		camera->SetViewportScissor(commandBuffer);
		
		if (mScreenRects.size()) {
			GraphicsPipeline* pipeline = ui->GetGraphics(commandBuffer->CurrentShaderPass(), { "SCREEN_SPACE" });
			if (pipeline) {
				commandBuffer->BindPipeline(pipeline);

				Buffer* screenRects = commandBuffer->GetBuffer("ScreenRects", mScreenRects.size() * sizeof(GuiRect), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
				memcpy(screenRects->MappedData(), mScreenRects.data(), mScreenRects.size() * sizeof(GuiRect));

				DescriptorSet* ds = commandBuffer->GetDescriptorSet("ScreenRects", pipeline->mDescriptorSetLayouts[PER_OBJECT]);
				ds->CreateBufferDescriptor("Rects", screenRects, 0, mScreenRects.size() * sizeof(GuiRect), pipeline);
				commandBuffer->BindDescriptorSet(ds, PER_OBJECT);

				commandBuffer->PushConstantRef("ScreenSize", screenSize);

				((vk::CommandBuffer)*commandBuffer).draw(6, (uint32_t)mScreenRects.size(), 0, 0);
				commandBuffer->mTriangleCount += mScreenRects.size()*2;
			}
		}
		if (mScreenTextureRects.size()) {
			GraphicsPipeline* pipeline = ui->GetGraphics(commandBuffer->CurrentShaderPass(), { "SCREEN_SPACE", "TEXTURED" });
			if (pipeline) {
				commandBuffer->BindPipeline(pipeline);

				Buffer* screenRects = commandBuffer->GetBuffer("ScreenRects", mScreenTextureRects.size() * sizeof(GuiRect), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
				memcpy(screenRects->MappedData(), mScreenTextureRects.data(), mScreenTextureRects.size() * sizeof(GuiRect));

				DescriptorSet* ds = commandBuffer->GetDescriptorSet("ScreenRects", pipeline->mDescriptorSetLayouts[PER_OBJECT]);
				ds->CreateBufferDescriptor("Rects", screenRects, 0, mScreenTextureRects.size() * sizeof(GuiRect), pipeline);
				for (uint32_t i = 0; i < mTextureArray.size(); i++)
					ds->CreateTextureDescriptor("Textures", mTextureArray[i], pipeline, i);
				commandBuffer->BindDescriptorSet(ds, PER_OBJECT);

				commandBuffer->PushConstantRef("ScreenSize", screenSize);

				((vk::CommandBuffer)*commandBuffer).draw(6, (uint32_t)mScreenTextureRects.size(), 0, 0);
				commandBuffer->mTriangleCount += mScreenTextureRects.size()*2;
			}
		}
		if (mScreenLines.size()) {
			GraphicsPipeline* pipeline = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/line.stmb")->GetGraphics(commandBuffer->CurrentShaderPass(), { "SCREEN_SPACE" });
			if (pipeline) {
				commandBuffer->BindPipeline(pipeline, vk::PrimitiveTopology::eLineStrip);

				Buffer* pts = commandBuffer->GetBuffer("Line Pts", sizeof(float3) * mLinePoints.size(), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
				Buffer* transforms = commandBuffer->GetBuffer("Line Transforms", sizeof(float4x4) * mLineTransforms.size(), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
				
				memcpy(transforms->MappedData(), mLineTransforms.data(), sizeof(float4x4) * mLineTransforms.size());
				memcpy(pts->MappedData(), mLinePoints.data(), sizeof(float3) * mLinePoints.size());
				
				DescriptorSet* ds = commandBuffer->GetDescriptorSet("ScreenLines", pipeline->mDescriptorSetLayouts[PER_OBJECT]);
				ds->CreateBufferDescriptor("Vertices", pts, 0, sizeof(float3) * mLinePoints.size(), pipeline);
				ds->CreateBufferDescriptor("Transforms", transforms, 0, sizeof(float4x4) * mLineTransforms.size(), pipeline);
				commandBuffer->BindDescriptorSet(ds, PER_OBJECT);

				commandBuffer->PushConstantRef("ScreenSize", screenSize);

				for (const GuiLine& l : mScreenLines) {
					commandBuffer->PushConstantRef("Color", l.mColor);
					commandBuffer->PushConstantRef("ClipBounds", l.mClipBounds);
					commandBuffer->PushConstantRef("TransformIndex", l.mTransformIndex);
					((vk::CommandBuffer)*commandBuffer).setLineWidth(l.mThickness);
					((vk::CommandBuffer)*commandBuffer).draw(l.mCount, 1, l.mIndex, 0);
				}
			}
		}
		if (mScreenStrings.size() && mStringGlyphs.size()) {
			camera->SetViewportScissor(commandBuffer);
			GraphicsPipeline* pipeline = font->GetGraphics(commandBuffer->CurrentShaderPass(), { "SCREEN_SPACE" });
			if (pipeline) {
				commandBuffer->BindPipeline(pipeline);

				DescriptorSet* ds = commandBuffer->GetDescriptorSet("Screen Strings DescriptorSet", pipeline->mDescriptorSetLayouts[PER_OBJECT]);
				ds->CreateBufferDescriptor("Glyphs", glyphBuffer, 0, mStringGlyphs.size() * sizeof(GlyphRect), pipeline);
				for (auto s : mScreenStrings)
					ds->CreateTextureDescriptor("SDFs", s.mFont->SDF(), pipeline, s.mSdfIndex);
				commandBuffer->BindDescriptorSet(ds, PER_OBJECT);

				commandBuffer->PushConstantRef("ScreenSize", screenSize);

				for (uint32_t i = 0; i < mScreenStrings.size(); i++) {
					const GuiString& s = mScreenStrings[i];
					commandBuffer->PushConstantRef("Color", s.mColor);
					commandBuffer->PushConstantRef("ClipBounds", s.mClipBounds);
					commandBuffer->PushConstantRef("Depth", s.mDepth);
					commandBuffer->PushConstantRef("SdfIndex", s.mSdfIndex);
					((vk::CommandBuffer)*commandBuffer).draw(s.mGlyphCount*6, 1, s.mGlyphIndex*6, i);
					commandBuffer->mTriangleCount += s.mGlyphCount*2;
				}
			}
		}
	}
}


void GuiContext::PolyLine(const float3* points, uint32_t pointCount, const float4& color, float thickness, const float3& offset, const float3& scale, const fRect2D& clipRect) {
	GuiLine l;
	l.mColor = color;
	l.mClipBounds = clipRect;
	l.mCount = pointCount;
	l.mIndex = (uint32_t)mLinePoints.size();
	l.mThickness = thickness;
	l.mTransformIndex = (uint32_t)mLineTransforms.size();
	mScreenLines.push_back(l);
	mLineTransforms.push_back(float4x4::TRS(offset, quaternion(0,0,0,1), scale));

	mLinePoints.resize(mLinePoints.size() + pointCount);
	memcpy(mLinePoints.data() + l.mIndex, points, pointCount * sizeof(float3));
}
void GuiContext::PolyLine(const float4x4& transform, const float3* points, uint32_t pointCount, const float4& color, float thickness, const fRect2D& clipRect) {
	GuiLine l;
	l.mColor = color;
	l.mClipBounds = clipRect;
	l.mThickness = thickness;
	l.mIndex = (uint32_t)mLinePoints.size();
	l.mCount = pointCount;
	l.mTransformIndex = (uint32_t)mLineTransforms.size();
	mWorldLines.push_back(l);
	mLineTransforms.push_back(transform);

	mLinePoints.resize(mLinePoints.size() + pointCount);
	memcpy(mLinePoints.data() + l.mIndex, points, pointCount * sizeof(float3));
}

void GuiContext::WireCube(const float3& center, const float3& extents, const quaternion& rotation, const float4& color) {
	// TODO: implement WireCube
}
void GuiContext::WireCircle(const float3& center, float radius, const quaternion& rotation, const float4& color) {
	// TODO: implement WireCircle
}
void GuiContext::WireSphere(const float3& center, float radius, const float4& color) {
	WireCircle(center, radius, quaternion(0,0,0,1), color);
	WireCircle(center, radius, quaternion(0, .70710678f, 0, .70710678f), color);
	WireCircle(center, radius, quaternion(.70710678f, 0, 0, .70710678f), color);
}

bool GuiContext::PositionHandle(const string& name, const quaternion& plane, float3& position, float radius, const float4& color) {
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
			mHotControl[p->mName] = (uint32_t)controlId;

			float3 fwd = plane * float3(0,0,1);
			float3 pos  = p->mWorldRay.mOrigin + p->mWorldRay.mDirection * p->mWorldRay.Intersect(fwd, position);
			float3 posLast = lp->mWorldRay.mOrigin + lp->mWorldRay.mDirection * lp->mWorldRay.Intersect(fwd, position);

			position += pos - posLast;

			ret = true;
		}
	
	return ret;
}
bool GuiContext::RotationHandle(const string& name, const float3& center, quaternion& rotation, float radius, float sensitivity) {
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
			mHotControl[p->mName] = (uint32_t)controlId;

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


void GuiContext::DrawString(const float2& screenPos, float z, Font* font, float pixelHeight, const string& str, const float4& color, TextAnchor horizontalAnchor, const fRect2D& clipRect) {
	if (str.empty()) return;
	
	GuiString s = {};
	
	s.mGlyphIndex = (uint32_t)mStringGlyphs.size();

	AABB aabb;
	font->GenerateGlyphs(mStringGlyphs, aabb, str, pixelHeight, screenPos, horizontalAnchor);
	s.mGlyphCount = (uint32_t)(mStringGlyphs.size() - s.mGlyphIndex);
	fRect2D r(aabb.mMin.xy, aabb.Size().xy);
	if (s.mGlyphCount == 0 || !r.Intersects(clipRect)) return;

	s.mColor = color;
	s.mClipBounds = clipRect;
	s.mDepth = z;
	s.mFont = font;
	s.mSdfIndex = (uint32_t)mStringSDFs.size();
	for (uint32_t i = 0; i < mStringSDFs.size(); i++)
		if (mStringSDFs[i] == font->SDF()) {
			s.mSdfIndex = i;
			mScreenStrings.push_back(s);
			return;
		}
	mStringSDFs.push_back(font->SDF());
	mScreenStrings.push_back(s);
}
void GuiContext::DrawString(const float4x4& transform, const float2& offset, Font* font, float pixelHeight, const string& str, const float4& color, TextAnchor horizontalAnchor, const fRect2D& clipRect) {
	if (str.empty()) return;

	GuiString s;
	s.mSdfIndex = (uint32_t)mStringSDFs.size();
	for (uint32_t i = 0; i < mStringSDFs.size(); i++)
		if (mStringSDFs[i] == font->SDF()) {
			s.mSdfIndex = i;
			break;
		}
	if (s.mSdfIndex == mStringSDFs.size()) mStringSDFs.push_back(font->SDF());

	s.mGlyphIndex = (uint32_t)mStringGlyphs.size();

	AABB aabb;
	font->GenerateGlyphs(mStringGlyphs, aabb, str, pixelHeight, offset, horizontalAnchor);
	s.mGlyphCount = (uint32_t)(mStringGlyphs.size() - s.mGlyphIndex);
	fRect2D r(aabb.Center().xy, aabb.HalfSize().xy);
	if (s.mGlyphCount == 0 || !r.Intersects(clipRect)) { return; }

	s.mColor = color;
	s.mClipBounds = clipRect;
	s.mTransformIndex = (uint32_t)mStringTransforms.size();
	s.mFont = font;

	mStringTransforms.push_back(transform);
	mWorldStrings.push_back(s);
	if (s.mSdfIndex == mStringSDFs.size()) mStringSDFs.push_back(font->SDF());
}

void GuiContext::Rect(const fRect2D& screenRect, float z, const float4& color, Texture* texture, const float4& textureST, const fRect2D& clipRect) {
	if (!screenRect.Intersects(clipRect)) return;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		float2 c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		if (screenRect.Contains(c) && clipRect.Contains(c)) const_cast<InputPointer*>(i->GetPointer(0))->mGuiHitT = 0.f;
	}

	GuiRect r;
	r.mScaleTranslate = float4(screenRect.mSize, screenRect.mOffset);
	r.mColor = color;
	r.mClipBounds = clipRect;
	r.mDepth = z;
	r.mTextureST = textureST;

	if (texture) {
		if (mTextureMap.count(texture))
			r.mTextureIndex = mTextureMap.at(texture);
		else {
			r.mTextureIndex = (uint32_t)mTextureArray.size();
			mTextureMap.emplace(texture, (uint32_t)mTextureArray.size());
			mTextureArray.push_back(texture);
		}
		mScreenTextureRects.push_back(r);
	} else
		mScreenRects.push_back(r);
}
void GuiContext::Rect(const float4x4& transform, const fRect2D& rect, const float4& color, Texture* texture, const float4& textureST, const fRect2D& clipRect) {
	if (!rect.Intersects(clipRect)) return;

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


	GuiRect r;
	r.mTransform = transform;
	r.mScaleTranslate = float4(rect.mSize, rect.mOffset);
	r.mColor = color;
	r.mClipBounds = clipRect;
	r.mTextureST = textureST;

	if (texture) {
		if (mTextureMap.count(texture))
			r.mTextureIndex = mTextureMap.at(texture);
		else {
			r.mTextureIndex = (uint32_t)mTextureArray.size();
			mTextureMap.emplace(texture, (uint32_t)mTextureArray.size());
			mTextureArray.push_back(texture);
		}
		mWorldTextureRects.push_back(r);
	} else
		mWorldRects.push_back(r);
}

void GuiContext::Label(const fRect2D& screenRect, float z, Font* font, float fontPixelHeight, const string& text, const float4& textColor, const float4& bgcolor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect) {
	if (!screenRect.Intersects(clipRect)) return;
	if (bgcolor.a > 0) Rect(screenRect, z, bgcolor, nullptr, 0, clipRect);
	if (textColor.a > 0 && text.length()){
		float2 o = 0;
		if (horizontalAnchor == TextAnchor::eMid) o.x = screenRect.mSize.x * .5f;
		else if (horizontalAnchor == TextAnchor::eMax) o.x = screenRect.mSize.x - 2;

		if (verticalAnchor == TextAnchor::eMid) o.y = screenRect.mSize.y * .5f - font->Ascent(fontPixelHeight) * .25f;
		else if (verticalAnchor == TextAnchor::eMax) o.y = screenRect.mSize.y - font->Ascent(fontPixelHeight) - 2;
		else o.y = -font->Descent(fontPixelHeight) + 2;

		DrawString(screenRect.mOffset + o, z + DEPTH_DELTA, font, fontPixelHeight, text, textColor, horizontalAnchor, clipRect);
	}
}
void GuiContext::Label(const float4x4& transform, const fRect2D& rect, Font* font, float fontPixelHeight, const string& text, const float4& textColor, const float4& bgcolor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect) {
	if (!clipRect.Intersects(rect)) return;
	if (bgcolor.a > 0) Rect(transform, rect, bgcolor, nullptr, 0, clipRect);
	if (textColor.a > 0 && text.length()) {
		float2 o = 0;
		if (horizontalAnchor == TextAnchor::eMid) o.x = rect.mSize.x * .5f;
		else if (horizontalAnchor == TextAnchor::eMax) o.x = rect.mSize.x - 2;

		if (verticalAnchor == TextAnchor::eMid) o.y = rect.mSize.y * .5f - font->Ascent(fontPixelHeight) * .25f;
		else if (verticalAnchor == TextAnchor::eMax) o.y = rect.mSize.y - font->Ascent(fontPixelHeight) - 2;
		else o.y = -font->Descent(fontPixelHeight) + 2;

		DrawString(transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), rect.mOffset + o, font, fontPixelHeight, text, textColor, horizontalAnchor, clipRect);
	}
}

bool GuiContext::TextButton(const fRect2D& screenRect, float z, Font* font, float fontPixelHeight, const string& text, const float4& textColor, const float4& bgcolor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!screenRect.Intersects(clipRect)) return false;

	bool hover = false;
	bool click = false;
	bool ret = false;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		float2 c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		const InputPointer* p = i->GetPointer(0);

		hover = screenRect.Contains(c) && clipRect.Contains(c);
		click = p->mPrimaryButton && (hover || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

		if (hover || click) const_cast<InputPointer*>(i->GetPointer(0))->mGuiHitT = 0.f;
		if (click) mHotControl[p->mName] = controlId;
		ret = hover && (p->mPrimaryButton && !i->GetPointerLast(0)->mPrimaryButton);
	}

	fRect2D r = screenRect;
	float m = 1.f;
	if (hover) m = 1.2f;
	if (click) { m = 0.8f; r.mOffset += float2(1, -1); }
	
	if (bgcolor.a > 0)
		Rect(r, z, float4(bgcolor.rgb * m, bgcolor.a), nullptr, 0, clipRect);

	if (textColor.a > 0 && text.length()) {
		float2 o = 0;
		if (horizontalAnchor == TextAnchor::eMid) o.x = screenRect.mSize.x * .5f;
		else if (horizontalAnchor == TextAnchor::eMax) o.x = screenRect.mSize.x - 2;

		if (verticalAnchor == TextAnchor::eMid) o.y = screenRect.mSize.y * .5f - font->Ascent(fontPixelHeight) * .25f;
		else if (verticalAnchor == TextAnchor::eMax) o.y = screenRect.mSize.y - font->Ascent(fontPixelHeight) - 2;
		else o.y = -font->Descent(fontPixelHeight) + 2;

		DrawString(r.mOffset + o, z + DEPTH_DELTA, font, fontPixelHeight, text, float4(textColor.rgb * m, textColor.a), horizontalAnchor, clipRect);
	}
	return ret;
}
bool GuiContext::TextButton(const float4x4& transform, const fRect2D& rect, Font* font, float fontPixelHeight, const string& text, const float4& textColor, const float4& bgcolor, TextAnchor horizontalAnchor, TextAnchor verticalAnchor, const fRect2D& clipRect){
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

	if (bgcolor.a > 0)
		Rect(transform, r, float4(bgcolor.rgb * m, bgcolor.a), nullptr, 0, clipRect);
	
	if (textColor.a > 0 && text.length()) {
		float2 o = 0;
		if (horizontalAnchor == TextAnchor::eMid) o.x = r.mSize.x * .5f;
		else if (horizontalAnchor == TextAnchor::eMax) o.x = r.mSize.x - 2;

		if (verticalAnchor == TextAnchor::eMid) o.y = r.mSize.y * .5f - font->Ascent(fontPixelHeight) * .25f;
		else if (verticalAnchor == TextAnchor::eMax) o.y = r.mSize.y - font->Ascent(fontPixelHeight) - 2;
		else o.y = -font->Descent(fontPixelHeight) + 2;
		DrawString(transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), r.mOffset + o, font, fontPixelHeight, text, float4(textColor.rgb * m, textColor.a), horizontalAnchor, clipRect);
	}
	return hover && first;
}

bool GuiContext::ImageButton(const fRect2D& screenRect, float z, Texture* texture, const float4& color, const float4& textureST, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!screenRect.Intersects(clipRect)) return false;

	bool hover = false;
	bool click = false;
	bool ret = false;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		float2 c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		const InputPointer* p = i->GetPointer(0);

		hover = screenRect.Contains(c) && clipRect.Contains(c);
		click = p->mPrimaryButton && (hover || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

		if (hover || click) const_cast<InputPointer*>(i->GetPointer(0))->mGuiHitT = 0.f;
		if (click) mHotControl[p->mName] = controlId;
		ret = hover && (p->mPrimaryButton && !i->GetPointerLast(0)->mPrimaryButton);
	}

	if (color.a > 0) {
		fRect2D r = screenRect;
		float m = 1.f;
		if (hover) m = 1.2f;
		if (click) { m = 1.5f; r.mOffset += float2(1, -1); }
		Rect(r, z, float4(color.rgb * m, color.a), texture, textureST, clipRect);
	}
	return ret;
}
bool GuiContext::ImageButton(const float4x4& transform, const fRect2D& rect, Texture* texture, const float4& color, const float4& textureST, const fRect2D& clipRect) {
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

bool GuiContext::Slider(const fRect2D& screenRect, float z, float& value, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4& barColor, const float4& knobColor, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!screenRect.Intersects(clipRect)) return false;

	uint32_t scrollAxis = axis == LayoutAxis::eHorizontal ? 0 : 1;
	uint32_t otherAxis = axis == LayoutAxis::eHorizontal ? 1 : 0;

	bool hover = false;
	bool click = false;
	bool ret = false;

	fRect2D barRect = screenRect;
	fRect2D interactRect = screenRect;
	if (knobSize > interactRect.mSize[otherAxis]) {
		interactRect.mOffset[otherAxis] += interactRect.mSize[otherAxis] * .5f - knobSize * .5f;
		interactRect.mSize[otherAxis] = knobSize;
	}

	// Modify position from input
	MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>();
	float2 cursor = i->CursorPos();
	float2 lastCursor = i->LastCursorPos();
	cursor.y = i->WindowHeight() - cursor.y;
	lastCursor.y = i->WindowHeight() - lastCursor.y;
	const InputPointer* p = i->GetPointer(0);
	if ((i->KeyDown(MOUSE_LEFT) && mLastHotControl[p->mName] == controlId) || (i->KeyDownFirst(MOUSE_LEFT) && interactRect.Contains(cursor)) ) {
		value = minimum + (cursor[scrollAxis] - barRect.mOffset[scrollAxis]) / barRect.mSize[scrollAxis] * (maximum - minimum);
		ret = true;
		hover = true;
		click = true;
	}

	// Derive modified value from modified position
	value = clamp(value, minimum, maximum);
	
	fRect2D knobRect(barRect.mOffset, knobSize);
	knobRect.mOffset[scrollAxis] += (value - minimum) / (maximum - minimum) * barRect.mSize[scrollAxis];
	knobRect.mOffset[otherAxis] += barRect.mSize[otherAxis] *.5f;
	knobRect.mOffset -= knobSize*.5f;

	bool hvr = (interactRect.Contains(cursor)) && clipRect.Contains(cursor);

	if (hover || click) const_cast<InputPointer*>(i->GetPointer(0))->mGuiHitT = 0.f;
	if (click) mHotControl[p->mName] = controlId;

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(barRect, z, barColor, nullptr, 0, clipRect);
	Rect(knobRect, z + DEPTH_DELTA, float4(knobColor.rgb * m, knobColor.a), mIconsTexture, ICON_CIRCLE_ST, clipRect);

	return ret;
}
bool GuiContext::Slider(const float4x4& transform, const fRect2D& rect, float& value, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4& barColor, const float4& knobColor, const fRect2D& clipRect) {
	uint32_t controlId = mNextControlId++;
	if (!clipRect.Intersects(rect)) return false;

	bool ret = false;

	fRect2D barRect = rect;

	uint32_t scrollAxis = axis == LayoutAxis::eHorizontal ? 0 : 1;
	uint32_t otherAxis = axis == LayoutAxis::eHorizontal ? 1 : 0;

	// Determine knob position
	float knobPos = barRect.mOffset[scrollAxis] + (value - minimum) / (maximum - minimum) * barRect.mSize[scrollAxis];

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
				knobPos += p->mScrollDelta[scrollAxis] * barRect.mSize[scrollAxis] * .25f;

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
	value = minimum + (knobPos - barRect.mOffset[scrollAxis]) / barRect.mSize[scrollAxis] * (maximum - minimum);
	value = clamp(value, minimum, maximum);
	knobPos = barRect.mOffset[scrollAxis] + (value - minimum) / (maximum - minimum) * barRect.mSize[scrollAxis];

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

bool GuiContext::RangeSlider(const fRect2D& screenRect, float z, float2& values, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4& barColor, const float4& knobColor, const fRect2D& clipRect) {
	uint32_t controlId[2];
	controlId[0] = mNextControlId++;
	controlId[1] = mNextControlId++;
	if (!screenRect.Intersects(clipRect)) return false;

	uint32_t scrollAxis = axis == LayoutAxis::eHorizontal ? 0 : 1;
	uint32_t otherAxis = axis == LayoutAxis::eHorizontal ? 1 : 0;

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
				values[j] = minimum + (cursor[scrollAxis] - barRect.mOffset[scrollAxis]) / barRect.mSize[scrollAxis] * (maximum - minimum);
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
	knobRects[0].mOffset[scrollAxis] += (values[0] - minimum) / (maximum - minimum) * barRect.mSize[scrollAxis];
	knobRects[1].mOffset[scrollAxis] += (values[1] - minimum) / (maximum - minimum) * barRect.mSize[scrollAxis];
	knobRects[0].mOffset[otherAxis] += barRect.mSize[otherAxis] * .5f;
	knobRects[1].mOffset[otherAxis] += barRect.mSize[otherAxis] * .5f;
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
	middleRect.mSize[scrollAxis] = fabsf(knobRects[1].mOffset[scrollAxis] - knobRects[0].mOffset[scrollAxis]);

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(barRect, z, barColor, nullptr, 0, clipRect);
	Rect(knobRects[0], z, float4(knobColor.rgb * m, knobColor.a), mIconsTexture, ICON_TRI_RIGHT_ST, clipRect);
	Rect(knobRects[1], z, float4(knobColor.rgb * m, knobColor.a), mIconsTexture, ICON_TRI_LEFT_ST, clipRect);
	if (middleRect.mSize[scrollAxis] > 0)
		Rect(middleRect, z, float4(knobColor.rgb * m, knobColor.a), nullptr, 0, clipRect);
	return ret;
}
bool GuiContext::RangeSlider(const float4x4& transform, const fRect2D& rect, float2& values, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4& barColor, const float4& knobColor, const fRect2D& clipRect) {
	uint32_t controlIds[3] = { mNextControlId++, mNextControlId++, mNextControlId++ };
	if (!clipRect.Intersects(rect)) return false;

	bool ret = false;

	fRect2D barRect = rect;

	uint32_t scrollAxis = axis == LayoutAxis::eHorizontal ? 0 : 1;
	uint32_t otherAxis = axis == LayoutAxis::eHorizontal ? 1 : 0;

	// Determine knob positions
	float2 pos = barRect.mOffset[scrollAxis] + (values - minimum) / (maximum - minimum) * barRect.mSize[scrollAxis];

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
					pos[0] += p->mScrollDelta[scrollAxis] * barRect.mSize[scrollAxis] * .25f;
				else if (c.x > pos[1] - knobSize * .5f)
					pos[1] += p->mScrollDelta[scrollAxis] * barRect.mSize[scrollAxis] * .25f;
				else
					pos += p->mScrollDelta[scrollAxis] * barRect.mSize[scrollAxis] * .25f;
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
	values = minimum + (pos - barRect.mOffset[scrollAxis]) / barRect.mSize[scrollAxis] * (maximum - minimum);
	values = clamp(values, minimum, maximum);
	if (values.x > values.y) swap(values.x, values.y);

	// Derive final knob position from the modified, clamped value
	pos[0] = barRect.mOffset[scrollAxis] + (values[0] - minimum) / (maximum - minimum) * barRect.mSize[scrollAxis];
	pos[1] = barRect.mOffset[scrollAxis] + (values[1] - minimum) / (maximum - minimum) * barRect.mSize[scrollAxis];

	fRect2D middleRect = rect;
	middleRect.mOffset[otherAxis] += rect.mSize[otherAxis] * .125f;
	middleRect.mSize[otherAxis] *= 0.75f;
	middleRect.mOffset[scrollAxis] = pos[0] + knobSize;
	middleRect.mSize[scrollAxis] = pos[1] - (pos[0] + knobSize);

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

			if (middleRect.mSize[scrollAxis] > 0) {
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
	if (middleRect.mSize[scrollAxis] > 0)
		Rect(transform * float4x4::Translate(float3(0, 0, DEPTH_DELTA)), middleRect, float4(knobColor.rgb * m, knobColor.a), nullptr, 0, clipRect);
	return ret;
}


fRect2D GuiContext::GuiLayout::Get(float size) {
	fRect2D layoutRect = mRect;
	switch (mAxis) {
	case LayoutAxis::eVertical:
		layoutRect.mSize.y = size;
		layoutRect.mOffset.y += (mRect.mSize.y - size) - mLayoutPosition;
		break;
	case LayoutAxis::eHorizontal:
		layoutRect.mSize.x = size;
		layoutRect.mOffset.x += mLayoutPosition;
		break;
	}
	mLayoutPosition += size;
	return layoutRect;
}

fRect2D GuiContext::BeginScreenLayout(LayoutAxis axis, const fRect2D& screenRect) {
	fRect2D layoutRect(screenRect.mOffset + mLayoutTheme.mControlPadding, screenRect.mSize - mLayoutTheme.mControlPadding * 2);
	mLayoutStack.push({ float4x4(1), true, axis, layoutRect, layoutRect, 0, START_DEPTH + DEPTH_DELTA });
	if (mLayoutTheme.mBackgroundColor.a > 0) Rect(screenRect, START_DEPTH, mLayoutTheme.mBackgroundColor, nullptr, 0);
	return layoutRect;
}
fRect2D GuiContext::BeginWorldLayout(LayoutAxis axis, const float4x4& tranform, const fRect2D& rect) {
	fRect2D layoutRect(rect.mOffset + mLayoutTheme.mControlPadding, rect.mSize - mLayoutTheme.mControlPadding * 2);
	mLayoutStack.push({ tranform, false, axis, layoutRect, layoutRect, 0, START_DEPTH + DEPTH_DELTA });
	if (mLayoutTheme.mBackgroundColor.a > 0) Rect(tranform * float4x4::Translate(float3(0, 0, START_DEPTH)), rect, mLayoutTheme.mBackgroundColor);
	return layoutRect;
}

fRect2D GuiContext::BeginSubLayout(LayoutAxis axis, float size) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(size);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (mLayoutTheme.mBackgroundColor.a > 0) {
		if (l.mScreenSpace)
			Rect(layoutRect, l.mLayoutDepth + DEPTH_DELTA, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mClipRect);
		else
			Rect(l.mTransform + float4x4::Translate(float3(0, 0, l.mLayoutDepth + DEPTH_DELTA)), layoutRect, mLayoutTheme.mBackgroundColor, nullptr, 0, l.mClipRect);
	}

	layoutRect.mOffset += mLayoutTheme.mControlPadding;
	layoutRect.mSize -= mLayoutTheme.mControlPadding * 2;

	mLayoutStack.push({ l.mTransform, l.mScreenSpace, axis, layoutRect, layoutRect, 0, l.mLayoutDepth + 2*DEPTH_DELTA });

	return layoutRect;
}
fRect2D GuiContext::BeginScrollSubLayout(float size, float contentSize) {
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
				scrollAmount -= i->ScrollDelta()*10;
				const_cast<InputPointer*>(i->GetPointer(0))->mGuiHitT = 0.f;
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
					scrollAmount -= p->mScrollDelta[l.mAxis == LayoutAxis::eHorizontal ? 0 : 1] * contentSize * .25f;
					const_cast<InputPointer*>(p)->mGuiHitT = t;
				}
			}
	}

	float scrollMax = max(0.f, contentSize - layoutRect.mSize.y);
	scrollAmount = clamp(scrollAmount, 0.f, scrollMax);

	mControlData[controlId] = scrollAmount;

	if (mLayoutTheme.mBackgroundColor.a > 0) {
		float4 c = mLayoutTheme.mBackgroundColor;
		c.rgb *= 0.75f;
		if (l.mScreenSpace)
			Rect(layoutRect, l.mLayoutDepth, c, nullptr, 0, l.mClipRect);
		else
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, c, nullptr, 0, l.mClipRect);
	}

	fRect2D contentRect = layoutRect;
	contentRect.mOffset += mLayoutTheme.mControlPadding;
	contentRect.mSize -= mLayoutTheme.mControlPadding * 2;
	switch (l.mAxis) {
	case LayoutAxis::eHorizontal:
		contentRect.mOffset.x -= scrollAmount + (layoutRect.mSize.x - contentSize);
		contentRect.mSize.x = contentSize - mLayoutTheme.mControlPadding * 2;
		break;
	case LayoutAxis::eVertical:
		contentRect.mOffset.y += (layoutRect.mSize.y - contentSize) + scrollAmount;
		contentRect.mSize.y = contentSize - mLayoutTheme.mControlPadding * 2;
		break;
	}
	
	// scroll bar slider
	if (scrollMax > 0) {
		fRect2D slider;
		fRect2D sliderbg;

		switch (l.mAxis) {
		case LayoutAxis::eHorizontal:
			slider.mSize = float2(layoutRect.mSize.x * (layoutRect.mSize.x / contentSize), mLayoutTheme.mScrollBarThickness);
			slider.mOffset = layoutRect.mOffset + float2((layoutRect.mSize.x - slider.mSize.x) * (scrollAmount / scrollMax), 0);
			sliderbg.mOffset = layoutRect.mOffset;
			sliderbg.mSize = float2(layoutRect.mSize.x, slider.mSize.y);

			contentRect.mOffset.y += slider.mSize.y;
			layoutRect.mOffset.y += slider.mSize.y;
			layoutRect.mSize.y -= slider.mSize.y;
			break;

		case LayoutAxis::eVertical:
			slider.mSize = float2(mLayoutTheme.mScrollBarThickness, layoutRect.mSize.y * (layoutRect.mSize.y / contentSize));
			slider.mOffset = layoutRect.mOffset + float2(layoutRect.mSize.x - slider.mSize.x, (layoutRect.mSize.y - slider.mSize.y) * (1 - scrollAmount / scrollMax));
			sliderbg.mOffset = layoutRect.mOffset + float2(layoutRect.mSize.x - slider.mSize.x, 0);
			sliderbg.mSize = float2(slider.mSize.x, layoutRect.mSize.y);

			layoutRect.mSize.x -= slider.mSize.x;
			contentRect.mSize.x -= slider.mSize.x;
			break;
		}

		uint32_t scrollAxis = l.mAxis == LayoutAxis::eHorizontal ? 0 : 1;
		uint32_t otherAxis = l.mAxis == LayoutAxis::eHorizontal ? 1 : 0;

		float2 offset = slider.mOffset;
		float extent = slider.mSize[otherAxis];
		slider.mOffset[scrollAxis] += extent / 2;
		slider.mSize[scrollAxis] = fmaxf(0, slider.mSize[scrollAxis] - extent);
		
		if (l.mScreenSpace) {
			Rect(slider, l.mLayoutDepth + DEPTH_DELTA, mLayoutTheme.mSliderColor, nullptr, 0);
			Rect(slider, l.mLayoutDepth + 2*DEPTH_DELTA, mLayoutTheme.mSliderKnobColor, nullptr, 0);
			
			Rect(fRect2D(offset, extent), l.mLayoutDepth + 2*DEPTH_DELTA, mLayoutTheme.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
			offset[scrollAxis] += floorf(slider.mSize[scrollAxis] - 0.5f);
			Rect(fRect2D(offset, extent), l.mLayoutDepth + 2*DEPTH_DELTA, mLayoutTheme.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
		} else {
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + DEPTH_DELTA)), sliderbg, mLayoutTheme.mSliderColor);
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), slider, mLayoutTheme.mSliderKnobColor);
			
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), fRect2D(offset, extent), mLayoutTheme.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
			offset[scrollAxis] += slider.mSize[scrollAxis];
			Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), fRect2D(offset, extent), mLayoutTheme.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
		}
	}

	mLayoutStack.push({ l.mTransform, l.mScreenSpace, l.mAxis, contentRect, layoutRect, 0, l.mLayoutDepth + 3*DEPTH_DELTA });

	return contentRect;
}
void GuiContext::EndLayout() {
	mLayoutStack.pop();
}

void GuiContext::LayoutSpace(float size) {
	mLayoutStack.top().mLayoutPosition += size;
}
void GuiContext::LayoutSeparator() {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(2);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace)
		GuiContext::Rect(layoutRect, l.mLayoutDepth, mLayoutTheme.mTextColor, nullptr, 0, l.mClipRect);
	else
		GuiContext::Rect(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mTextColor, nullptr, 0, l.mClipRect);
}
void GuiContext::LayoutTitle(const string& text, TextAnchor horizontalAnchor, TextAnchor verticalAnchor) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mTitleFontHeight);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace)
		Label(layoutRect, l.mLayoutDepth, mLayoutTheme.mTitleFont, mLayoutTheme.mTitleFontHeight, text, mLayoutTheme.mTextColor, 0, horizontalAnchor, verticalAnchor, l.mClipRect);
	else
		Label(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mTitleFont, mLayoutTheme.mTitleFontHeight, text, mLayoutTheme.mTextColor, 0, horizontalAnchor, verticalAnchor, l.mClipRect);
}
void GuiContext::LayoutLabel(const string& text, TextAnchor horizontalAnchor, TextAnchor verticalAnchor) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace)
		Label(layoutRect, l.mLayoutDepth, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, text, mLayoutTheme.mTextColor, 0, horizontalAnchor, verticalAnchor, l.mClipRect);
	else
		Label(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, text, mLayoutTheme.mTextColor, 0, horizontalAnchor, verticalAnchor, l.mClipRect);
}
bool GuiContext::LayoutTextButton(const string& text, TextAnchor horizontalAnchor, TextAnchor verticalAnchor) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace)
		return TextButton(layoutRect, l.mLayoutDepth, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, text, mLayoutTheme.mTextColor, mLayoutTheme.mControlBackgroundColor, horizontalAnchor, verticalAnchor, l.mClipRect);
	else
		return TextButton(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, text, mLayoutTheme.mTextColor, mLayoutTheme.mControlBackgroundColor, horizontalAnchor, verticalAnchor, l.mClipRect);
}
bool GuiContext::LayoutImageButton(const float2& size, Texture* texture, const float4& textureST) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(size[(uint32_t)l.mAxis]);
	LayoutSpace(mLayoutTheme.mControlPadding);

	layoutRect.mSize = size;

	if (l.mScreenSpace)
		return ImageButton(layoutRect, l.mLayoutDepth, texture, mLayoutTheme.mTextColor, textureST, l.mClipRect);
	else
		return ImageButton(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, texture, mLayoutTheme.mTextColor, textureST, l.mClipRect);
}
bool GuiContext::LayoutToggle(const string& label, bool& value) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	if (l.mScreenSpace) {
		Label(layoutRect, l.mLayoutDepth, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, label, mLayoutTheme.mTextColor, 0, TextAnchor::eMin, TextAnchor::eMid, l.mClipRect);
		layoutRect.mOffset.x += layoutRect.mSize.x - mLayoutTheme.mControlSize;
		layoutRect.mSize = mLayoutTheme.mControlSize;
		if (ImageButton(layoutRect, l.mLayoutDepth, mIconsTexture, mLayoutTheme.mTextColor, value ? ICON_CHECK_ST : ICON_CHECKBOX_ST, l.mClipRect)) {
			value = !value;
			return true;
		}
		return false;
	} else {
		Label(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, label, mLayoutTheme.mTextColor, 0, TextAnchor::eMin, TextAnchor::eMid, l.mClipRect);
		layoutRect.mOffset.x += layoutRect.mSize.x - mLayoutTheme.mControlSize;
		layoutRect.mSize = mLayoutTheme.mControlSize;
		if (ImageButton(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), layoutRect, mIconsTexture, mLayoutTheme.mTextColor, value ? ICON_CHECK_ST : ICON_CHECKBOX_ST, l.mClipRect)) {
			value = !value;
			return true;
		}
		return false;
	}
}
bool GuiContext::LayoutSlider(const string& label, float& value, float minimum, float maximum) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	// make the bar the right size
	layoutRect.mOffset[(uint32_t)l.mAxis] += (layoutRect.mSize[(uint32_t)l.mAxis] - mLayoutTheme.mSliderBarSize) * .5f;
	layoutRect.mSize[(uint32_t)l.mAxis] = mLayoutTheme.mSliderBarSize;

	LayoutAxis axis = l.mAxis == LayoutAxis::eHorizontal ? LayoutAxis::eVertical : LayoutAxis::eHorizontal;
	if (l.mScreenSpace) {
		fRect2D labelRect = layoutRect;
		labelRect.mSize.x *= .25f;
		Label(labelRect, l.mLayoutDepth, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, label, mLayoutTheme.mTextColor, 0, TextAnchor::eMin, TextAnchor::eMid, l.mClipRect);
		layoutRect.mOffset.x += labelRect.mSize.x + mLayoutTheme.mSliderKnobSize*.5f;
		layoutRect.mSize.x -= labelRect.mSize.x + mLayoutTheme.mSliderKnobSize;
		return Slider(layoutRect, l.mLayoutDepth, value, minimum, maximum, axis, mLayoutTheme.mSliderKnobSize, mLayoutTheme.mSliderColor, mLayoutTheme.mSliderKnobColor, l.mClipRect);
	} else {
		fRect2D labelRect = layoutRect;
		labelRect.mSize.x *= .25f;
		Label(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), labelRect, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, label, mLayoutTheme.mTextColor, 0, TextAnchor::eMin, TextAnchor::eMid, l.mClipRect);
		layoutRect.mOffset.x += labelRect.mSize.x + mLayoutTheme.mSliderKnobSize*.5f;
		layoutRect.mSize.x -= labelRect.mSize.x + mLayoutTheme.mSliderKnobSize;
		return Slider(l.mTransform, layoutRect, value, minimum, maximum, axis, mLayoutTheme.mSliderKnobSize, mLayoutTheme.mSliderColor, mLayoutTheme.mSliderKnobColor, l.mClipRect);
	}
}
bool GuiContext::LayoutRangeSlider(const string& label, float2& values, float minimum, float maximum) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutTheme.mControlPadding);
	fRect2D layoutRect = l.Get(mLayoutTheme.mControlSize);
	LayoutSpace(mLayoutTheme.mControlPadding);

	// make the bar the right size
	layoutRect.mOffset[(uint32_t)l.mAxis] += (layoutRect.mSize[(uint32_t)l.mAxis] - mLayoutTheme.mSliderBarSize) * .5f;
	layoutRect.mSize[(uint32_t)l.mAxis] = mLayoutTheme.mSliderBarSize;

	LayoutAxis axis = l.mAxis == LayoutAxis::eHorizontal ? LayoutAxis::eVertical : LayoutAxis::eHorizontal;
	if (l.mScreenSpace) {
		fRect2D labelRect = layoutRect;
		labelRect.mSize.x *= .25f;
		Label(labelRect, l.mLayoutDepth, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, label, mLayoutTheme.mTextColor, 0, TextAnchor::eMin, TextAnchor::eMid, l.mClipRect);
		layoutRect.mOffset.x += labelRect.mSize.x + mLayoutTheme.mSliderKnobSize*.5f;
		layoutRect.mSize.x -= labelRect.mSize.x + mLayoutTheme.mSliderKnobSize;
		return RangeSlider(layoutRect, l.mLayoutDepth, values, minimum, maximum, axis, mLayoutTheme.mSliderKnobSize, mLayoutTheme.mSliderColor, mLayoutTheme.mSliderKnobColor, l.mClipRect);
	} else {
		fRect2D labelRect = layoutRect;
		labelRect.mSize.x *= .25f;
		Label(l.mTransform * float4x4::Translate(float3(0, 0, l.mLayoutDepth)), labelRect, mLayoutTheme.mControlFont, mLayoutTheme.mControlFontHeight, label, mLayoutTheme.mTextColor, 0, TextAnchor::eMin, TextAnchor::eMid, l.mClipRect);
		layoutRect.mOffset.x += labelRect.mSize.x + mLayoutTheme.mSliderKnobSize*.5f;
		layoutRect.mSize.x -= labelRect.mSize.x + mLayoutTheme.mSliderKnobSize;
		return RangeSlider(l.mTransform, layoutRect, values, minimum, maximum, axis, mLayoutTheme.mSliderKnobSize, mLayoutTheme.mSliderColor, mLayoutTheme.mSliderKnobColor, l.mClipRect);
	}
}