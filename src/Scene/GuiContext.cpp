#include "GuiContext.hpp"

#include "Camera.hpp"

using namespace stm;

const uint32_t CIRCLE_VERTEX_RESOLUTION = 64;

const float START_DEPTH = 1e-4f;
const float DEPTH_DELTA = -1e-6f;

const Vector4f ICON_CIRCLE_ST = Vector4f(128, 128, 256, 512) / 1024.f;
const Vector4f ICON_CHECKBOX_ST = Vector4f(128, 128, 0, 512) / 1024.f;
const Vector4f ICON_CHECK_ST = Vector4f(128, 128, 128, 512) / 1024.f;
const Vector4f ICON_TRI_RIGHT_ST = Vector4f(128, 128, 384, 512) / 1024.f;
const Vector4f ICON_TRI_LEFT_ST = Vector4f(128, 128, 512, 512) / 1024.f;

// TODO: replace ui input pointer calls with Scene::mInputPointers

GuiContext::GuiContext(stm::Scene& scene) : mScene(scene) {
	mIconsTexture = mScene.mInstance.Device().FindOrLoadAsset<Texture>("Assets/Textures/icons.png");

	SpirvModuleGroup uiSpirv(scene.mInstance.Device(), "Assets/Shaders/ui.stmb");
	
	mMaterials["ui"] = make_shared<Material>("GuiContext/UI", uiSpirv);
	mMaterials["font_ss"] = make_shared<MaterialDerivative>("GuiContext/UI", mMaterials["ui"], { "gScreenSpace", true });
	mMaterials["ui_ss"]->SetSpecialization("gScreenSpace", true);
	mMaterials["lines_ss"]->SetSpecialization("gScreenSpace", true);
}

void GuiContext::OnDraw(CommandBuffer& commandBuffer, Camera& camera) {
	Vector2f screenSize = Vector2f((float)commandBuffer.CurrentFramebuffer()->Extent().width, (float)commandBuffer.CurrentFramebuffer()->Extent().height);

	auto font     = mMaterials["font"];
	auto ui       = mMaterials["ui"];
	auto lines    = mMaterials["lines"];
	auto font_ss  = mMaterials["font_ss"];
	auto ui_ss    = mMaterials["ui_ss"];
	auto lines_ss = mMaterials["lines_ss"];
	font_ss->SetSpecialization("gScreenSpace", true);
	ui_ss->SetSpecialization("gScreenSpace", true);
	lines_ss->SetSpecialization("gScreenSpace", true);

	shared_ptr<Buffer> glyphBuffer;
	shared_ptr<Buffer> glyphTransforms;
	if (mStringGlyphs.size()) {
		glyphBuffer = commandBuffer.GetBuffer("Glyphs", mStringGlyphs.size() * sizeof(GlyphRect), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		glyphBuffer->Copy(mStringGlyphs);
		if (mStringTransforms.size()) {
			glyphTransforms = commandBuffer.GetBuffer("Transforms", mStringTransforms.size() * sizeof(Matrix4f), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
			glyphTransforms->Copy(mStringTransforms);
		}
	}
	// TODO: mWorldLines (with polygonal extrusion for antialiasing)
	if (mWorldRects.size()) {
		auto screenRects = commandBuffer.GetBuffer("WorldRects", mWorldRects.size() * sizeof(GuiRect), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		screenRects->Copy(mWorldRects);
	
		ui->SetStorageBuffer("Rects", screenRects, 0, mWorldRects.size() * sizeof(GuiRect));
		for (uint32_t i = 0; i < mTextureArray.size(); i++)
			ui->SetSampledTexture("Textures", mTextureArray[i], i);
		ui->Bind(commandBuffer);
		
		camera.SetViewportScissor(commandBuffer, StereoEye::eLeft);
		commandBuffer->draw(6, (uint32_t)mWorldRects.size(), 0, 0);
		commandBuffer.mTriangleCount += mWorldRects.size()*2;
		if (camera.StereoMode() != StereoMode::eNone) {
			camera.SetViewportScissor(commandBuffer, StereoEye::eRight);
			commandBuffer->draw(6, (uint32_t)mWorldRects.size(), 0, 0);
			commandBuffer.mTriangleCount += mWorldRects.size()*2;
		}
	}
	if (mWorldStrings.size() && mStringGlyphs.size() && mStringTransforms.size()) {
		font->SetStorageBuffer("Glyphs", glyphBuffer, 0, mStringGlyphs.size() * sizeof(GlyphRect));
		font->SetStorageBuffer("Transforms", glyphTransforms, 0, mStringTransforms.size() * sizeof(Matrix4f));
		for (uint32_t i = 0; i < mUniqueStringSDFs.size(); i++)
			font->SetSampledTexture("SDFs", mUniqueStringSDFs[i], i);
		font->Bind(commandBuffer);
		
		for (uint32_t i = 0; i < mWorldStrings.size(); i++) {
			const GuiString& s = mWorldStrings[i];
			commandBuffer.PushConstantRef("Color", s.mColor);
			commandBuffer.PushConstantRef("ClipBounds", s.mClipBounds);
			commandBuffer.PushConstantRef("SdfIndex", s.mSdfIndex);
			camera.SetViewportScissor(commandBuffer, StereoEye::eLeft);
			commandBuffer->draw(s.mGlyphCount*6, 1, s.mGlyphIndex*6, i);
			commandBuffer.mTriangleCount += s.mGlyphCount*2;
			if (camera.StereoMode() != StereoMode::eNone) {
				camera.SetViewportScissor(commandBuffer, StereoEye::eRight);
				commandBuffer->draw(s.mGlyphCount*6, 1, s.mGlyphIndex*6, i);
				commandBuffer.mTriangleCount += s.mGlyphCount*2;
			}
		}
	}

	if (camera.StereoMode() == StereoMode::eNone) {
		camera.SetViewportScissor(commandBuffer);
		if (mScreenRects.size()) {
			auto screenRects = commandBuffer.GetBuffer("ScreenRects", mScreenRects.size() * sizeof(GuiRect), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
			screenRects->Copy(mScreenRects);

			ui_ss->SetStorageBuffer("Rects", screenRects, 0, mScreenRects.size() * sizeof(GuiRect));
			for (uint32_t i = 0; i < mTextureArray.size(); i++)
				ui_ss->SetSampledTexture("Textures", mTextureArray[i], i);
			ui_ss->Bind(commandBuffer);

			commandBuffer.PushConstantRef("ScreenSize", screenSize);

			commandBuffer->draw(6, (uint32_t)mScreenRects.size(), 0, 0);
			commandBuffer.mTriangleCount += mScreenRects.size()*2;
		}
		if (mScreenLines.size()) {
			auto pts = commandBuffer.GetBuffer("Line Pts", sizeof(Vector3f) * mLinePoints.size(), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
			pts->Copy(mLinePoints);
			auto transforms = commandBuffer.GetBuffer("Line Transforms", sizeof(Matrix4f) * mLineTransforms.size(), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
			transforms->Copy(mLineTransforms);
			
			lines_ss->SetStorageBuffer("Vertices", pts, 0, sizeof(Vector3f) * mLinePoints.size());
			lines_ss->SetStorageBuffer("Transforms", transforms, 0, sizeof(Matrix4f) * mLineTransforms.size());
			lines_ss->Bind(commandBuffer);

			commandBuffer.PushConstantRef("ScreenSize", screenSize);
			for (const GuiLine& l : mScreenLines) {
				commandBuffer.PushConstantRef("Color", l.mColor);
				commandBuffer.PushConstantRef("ClipBounds", l.mClipBounds);
				commandBuffer.PushConstantRef("TransformIndex", l.mTransformIndex);
				commandBuffer->setLineWidth(l.mThickness);
				commandBuffer->draw(l.mCount, 1, l.mIndex, 0);
			}
		}
		if (mScreenStrings.size() && mStringGlyphs.size()) {
			camera.SetViewportScissor(commandBuffer);
			font_ss->SetStorageBuffer("Glyphs", glyphBuffer, 0, mStringGlyphs.size() * sizeof(GlyphRect));
			for (uint32_t i = 0; i < mUniqueStringSDFs.size(); i++)
				font_ss->SetSampledTexture("SDFs", mUniqueStringSDFs[i], i);
			font_ss->Bind(commandBuffer);

			commandBuffer.PushConstantRef("ScreenSize", screenSize);

			for (uint32_t i = 0; i < mScreenStrings.size(); i++) {
				const GuiString& s = mScreenStrings[i];
				commandBuffer.PushConstantRef("Color", s.mColor);
				commandBuffer.PushConstantRef("ClipBounds", s.mClipBounds);
				commandBuffer.PushConstantRef("Depth", s.mDepth);
				commandBuffer.PushConstantRef("SdfIndex", s.mSdfIndex);
				commandBuffer->draw(s.mGlyphCount*6, 1, s.mGlyphIndex*6, i);
				commandBuffer.mTriangleCount += s.mGlyphCount*2;
			}
		}
	}
}

void GuiContext::WireCube(const Vector3f& center, const Vector3f& extents, const fquat& rotation, const Vector4f& color) {
	// TODO: implement WireCube
}
void GuiContext::WireCircle(const Vector3f& center, float radius, const fquat& rotation, const Vector4f& color) {
	// TODO: implement WireCircle
}
void GuiContext::WireSphere(const Vector3f& center, float radius, const fquat& rotation, const Vector4f& color) {
	WireCircle(center, radius, rotation, color);
	WireCircle(center, radius, rotation*fquat(0, .70710678f, 0, .70710678f), color);
	WireCircle(center, radius, rotation*fquat(.70710678f, 0, 0, .70710678f), color);
}

bool GuiContext::PositionHandle(const fquat& plane, Vector3f& position, float radius, const Vector4f& color) {
	WireCircle(position, radius, plane, color);
	
	size_t controlId = hash_combine(name);
	bool ret = false;
	
	InputState p, lp;
	Vector2f t, lt;
	bool hit, hitLast;
	//mScene.Intersect(Sphere(position, radius), t);

	if (!p.mPrimaryButton) {
		mHotControl.erase(p.Id());
		return false;
	}
	if ((mHotControl.count(p.Id()) == 0 || mHotControl.at(p.Id()) != controlId) && (!hit || !hitLast || t.x < 0 || lt.x < 0)) continue;
	mHotControl[p.Id()] = (uint32_t)controlId;

	position = p.Transform() * (lp.InverseTransform() * position);
	return true;
}
bool GuiContext::RotationHandle(const Vector3f& center, fquat& rotation, float radius, float sensitivity) {
	fquat r = rotation;
	WireCircle(center, radius, r, Vector4f(.2f,.2f,1,.5f));
	r *= fquat::Euler(0, (float)M_PI/2, 0);
	WireCircle(center, radius, r, Vector4f(1,.2f,.2f,.5f));
	r *= fquat::Euler((float)M_PI/2, 0, 0);
	WireCircle(center, radius, r, Vector4f(.2f,1,.2f,.5f));

	size_t controlId = hash_combine(name);
	bool ret = false;

	for (const InputSystem* d : mInputManager->InputDevices())
		for (uint32_t i = 0; i < d->PointerCount(); i++) {
			const InputPointer* p = d->GetPointer(i);
			const InputPointer* lp = d->GetPointerLast(i);

			Vector2f t, lt;
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

			Vector3f v = p->mWorldRay.mOrigin - center + p->mWorldRay.mDirection * t.x;
			Vector3f u = lp->mWorldRay.mOrigin - center + lp->mWorldRay.mDirection * lt.x;

			Vector3f rotAxis = cross(normalize(v), normalize(u));
			float angle = length(rotAxis);
			if (fabsf(angle) > .0001f)
				rotation = fquat::AxisAngle(rotAxis / angle, asinf(angle) * sensitivity) * rotation;

			ret = true;
	}
	return ret;
}


void GuiContext::PolyLine(const TransformInfo&, const Vector3f* points, uint32_t pointCount, float thickness) {
	GuiLine l;
	l.mColor = color;
	l.mClipBounds = clipRect;
	l.mCount = pointCount;
	l.mIndex = (uint32_t)mLinePoints.size();
	l.mThickness = thickness;
	l.mTransformIndex = (uint32_t)mLineTransforms.size();
	mScreenLines.push_back(l);
	mLineTransforms.push_back(Matrix4f::TRS(offset, fquat::Identity(), scale));

	mLinePoints.resize(mLinePoints.size() + pointCount);
	memcpy(mLinePoints.data() + l.mIndex, points, pointCount * sizeof(Vector3f));
}
void GuiContext::DrawString(const TransformInfo& transform, const string& str, const FontInfo& font) {
	if (str.empty()) return;
	
	GuiString s;
	s.mGlyphIndex = (uint32_t)mStringGlyphs.size();

	AlignedBox3f aabb;
	font->GenerateGlyphs(mStringGlyphs, aabb, str, pixelHeight, screenPos, horizontalAnchor);
	s.mGlyphCount = (uint32_t)(mStringGlyphs.size() - s.mGlyphIndex);
	AlignedBox2f r((Vector2f)aabb.mMin, (Vector2f)aabb.Size());
	if (s.mGlyphCount == 0 || !r.Intersects(clipRect)) return;

	s.mSdfIndex = (uint32_t)mUniqueStringSDFs.size();
	for (uint32_t i = 0; i < mUniqueStringSDFs.size(); i++)
		if (mUniqueStringSDFs[i] == font->SDF()) {
			s.mSdfIndex = i;
			break;
		}
	if (s.mSdfIndex == mUniqueStringSDFs.size()) mUniqueStringSDFs.push_back(font->SDF());

	s.mColor = color;
	s.mClipBounds = clipRect;
	s.mDepth = z;
	mScreenStrings.push_back(s);
}
bool GuiContext::Rect(const TransformInfo& transform, const TextureInfo& texture) {
	if (!transform.mRect.Intersects(transform.mClipRect)) return;

	GuiRect r;
	r.mScaleTranslate = Vector4f(transform.mRect.mSize, transform.mRect.mOffset);
	r.mColor = color;
	r.mClipBounds = transform.mClipRect;
	r.mDepth = z;
	r.mTextureST = texture.mScaleTranslate;

	if (texture.mTexture) {
		if (mTextureMap.count(texture.mTexture))
			r.mTextureIndex = mTextureMap.at(texture.mTexture);
		else {
			r.mTextureIndex = (uint32_t)mTextureArray.size();
			mTextureMap.emplace(texture.mTexture, (uint32_t)mTextureArray.size());
			mTextureArray.push_back(texture.mTexture);
		}
		mScreenRects.push_back(r);
	} else
		mScreenRects.push_back(r);
}
bool GuiContext::TextRect(const TransformInfo& transform, const string& str, const FontInfo& font) {
	uint32_t controlId = mNextControlId++;
	if (!transform.mRect.Intersects(transform.mClipRect)) return false;

	bool hover = false;
	bool click = false;
	bool ret = false;

	if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
		Vector2f c = i->CursorPos();
		c.y = i->WindowHeight() - c.y;
		const InputPointer* p = i->GetPointer(0);

		hover = screenRect.Contains(c) && clipRect.Contains(c);
		click = p->mPrimaryButton && (hover || (mLastHotControl.count(p->mName) && mLastHotControl.at(p->mName) == controlId));

		if (hover || click) const_cast<InputPointer*>(i->GetPointer(0))->mGuiHitT = 0.f;
		if (click) mHotControl[p->mName] = controlId;
		ret = hover && (p->mPrimaryButton && !i->GetPointerLast(0)->mPrimaryButton);
	}

	AlignedBox2f r = screenRect;
	float m = 1.f;
	if (hover) m = 1.2f;
	if (click) { m = 0.8f; r.mOffset += Vector2f(1, -1); }
	
	if (bgcolor.w > 0)
		Rect(r, z, Vector4f((Vector3f)bgcolor * m, bgcolor.w), nullptr, 0, clipRect);

	if (textColor.w > 0 && text.length()) {
		Vector2f o = 0;
		if (horizontalAnchor == TextAnchor::eMid) o.x = screenRect.mSize.x * .5f;
		else if (horizontalAnchor == TextAnchor::eMax) o.x = screenRect.mSize.x - 2;

		if (verticalAnchor == TextAnchor::eMid) o.y = screenRect.mSize.y * .5f - font->Ascent(fontPixelHeight) * .25f;
		else if (verticalAnchor == TextAnchor::eMax) o.y = screenRect.mSize.y - font->Ascent(fontPixelHeight) - 2;
		else o.y = -font->Descent(fontPixelHeight) + 2;

		DrawString(r.mOffset + o, z + DEPTH_DELTA, font, fontPixelHeight, text, Vector4f((Vector3f)textColor * m, textColor.w), horizontalAnchor, clipRect);
	}
	return ret;
}
bool GuiContext::Slider(const TransformInfo& transform, float& value, float minimum, float maximum) {
	uint32_t controlId = mNextControlId++;
	if (!transform.mRect.Intersects(transform.mClipRect)) return false;

	LayoutAxis axis = transform.mRect.mSize.y > transform.mRect.mSize.x ? LayoutAxis::eVertical : LayoutAxis::eHorizontal;
	uint32_t scrollAxis = axis == LayoutAxis::eHorizontal ? 0 : 1;
	uint32_t otherAxis = axis == LayoutAxis::eHorizontal ? 1 : 0;

	bool hover = false;
	bool click = false;
	bool ret = false;
	
	float w = transform.mRect.mSize[scrollAxis] - transform.mRect.mSize[otherAxis]/2;

	// Modify position from input
	MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>();
	Vector2f cursor = i->CursorPos();
	Vector2f lastCursor = i->LastCursorPos();
	cursor.y = i->WindowHeight() - cursor.y;
	lastCursor.y = i->WindowHeight() - lastCursor.y;
	const InputPointer* p = i->GetPointer(0);
	if ((i->KeyDown(MOUSE_LEFT) && mLastHotControl[p->mName] == controlId) || (i->KeyDownFirst(MOUSE_LEFT) && interactRect.Contains(cursor)) ) {
		value = minimum + (cursor[scrollAxis] - transform.mRect.mOffset[scrollAxis]) / transform.mRect.mSize[scrollAxis] * (maximum - minimum);
		ret = true;
		hover = true;
		click = true;
	}

	value = clamp(value, minimum, maximum);

	AlignedBox2f knobRect = transform.mRect;
	knobRect.mSize[scrollAxis] = knobRect.mSize[otherAxis];
	knobRect.mOffset[scrollAxis] += ( - knobRect.mSize[scrollAxis]) * (value - minimum) / (maximum - minimum);

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(transform);
	Rect(transform.Embed(knobRect), { mIconsTexture, ICON_CIRCLE_ST });

	return ret;
}
bool GuiContext::RangeSlider(const TransformInfo& transform, Vector2f& values, float minimum, float maximum) {
	uint32_t controlId[2];
	controlId[0] = mNextControlId++;
	controlId[1] = mNextControlId++;
	if (!transform.mRect.Intersects(transform.mClipRect)) return false;

	LayoutAxis axis = transform.mRect.mSize.y > transform.mRect.mSize.x ? LayoutAxis::eVertical : LayoutAxis::eHorizontal;
	uint32_t scrollAxis = axis == LayoutAxis::eHorizontal ? 0 : 1;
	uint32_t otherAxis = axis == LayoutAxis::eHorizontal ? 1 : 0;

	bool ret = false;
	bool hover = false;
	bool click = false;

	AlignedBox2f barRect = screenRect;

	MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>();
	Vector2f cursor = i->CursorPos();
	const InputPointer* p = i->GetPointer(0);
	Vector2f lastCursor = i->CursorPos();
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

	values = stm::clamp(values, minimum, maximum);

	AlignedBox2f knobRects[2];
	knobRects[0] = AlignedBox2f(barRect.mOffset, knobSize);
	knobRects[1] = AlignedBox2f(barRect.mOffset, knobSize);
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

	AlignedBox2f middleRect = barRect;
	middleRect.mOffset[scrollAxis] = min(knobRects[0].mOffset[scrollAxis], knobRects[1].mOffset[scrollAxis]) + knobSize * .5f;
	middleRect.mSize[scrollAxis] = abs(knobRects[1].mOffset[scrollAxis] - knobRects[0].mOffset[scrollAxis]);

	float m = 1.25f;
	if (hover) m *= 1.2f;
	if (click) m *= 1.5f;

	Rect(barRect, z, barColor, nullptr, 0, clipRect);
	Rect(knobRects[0], z, Vector4f((Vector3f)knobColor * m, knobColor.w), mIconsTexture, ICON_TRI_RIGHT_ST, clipRect);
	Rect(knobRects[1], z, Vector4f((Vector3f)knobColor * m, knobColor.w), mIconsTexture, ICON_TRI_LEFT_ST, clipRect);
	if (middleRect.mSize[scrollAxis] > 0)
		Rect(middleRect, z, Vector4f((Vector3f)knobColor * m, knobColor.w), nullptr, 0, clipRect);
	return ret;
}


AlignedBox2f GuiContext::BeginScreenLayout(LayoutAxis axis, const AlignedBox2f& screenRect) {
	AlignedBox2f layoutRect(screenRect.mOffset + mLayoutStyle.mControlPadding, screenRect.mSize - mLayoutStyle.mControlPadding * 2);
	mLayoutStack.push({ Matrix4f(1), true, axis, layoutRect, layoutRect, 0, START_DEPTH + DEPTH_DELTA });
	if (mLayoutStyle.mBackgroundColor.w > 0) Rect(screenRect, START_DEPTH, mLayoutStyle.mBackgroundColor, nullptr, 0);
	return layoutRect;
}
AlignedBox2f GuiContext::BeginWorldLayout(LayoutAxis axis, const Matrix4f& tranform, const AlignedBox2f& rect) {
	AlignedBox2f layoutRect(rect.mOffset + mLayoutStyle.mControlPadding, rect.mSize - mLayoutStyle.mControlPadding * 2);
	mLayoutStack.push({ tranform, false, axis, layoutRect, layoutRect, 0, START_DEPTH + DEPTH_DELTA });
	if (mLayoutStyle.mBackgroundColor.w > 0) Rect(tranform * Matrix4f::Translate(Vector3f(0, 0, START_DEPTH)), rect, mLayoutStyle.mBackgroundColor);
	return layoutRect;
}

AlignedBox2f GuiContext::BeginSubLayout(LayoutAxis axis, float size) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutStyle.mControlPadding);
	AlignedBox2f layoutRect = l.Get(size);
	LayoutSpace(mLayoutStyle.mControlPadding);

	if (mLayoutStyle.mBackgroundColor.w > 0) {
		if (l.mScreenSpace)
			Rect(layoutRect, l.mLayoutDepth + DEPTH_DELTA, mLayoutStyle.mBackgroundColor, nullptr, 0, l.mClipRect);
		else
			Rect(l.mCachedTransform + Matrix4f::Translate(Vector3f(0, 0, l.mLayoutDepth + DEPTH_DELTA)), layoutRect, mLayoutStyle.mBackgroundColor, nullptr, 0, l.mClipRect);
	}

	layoutRect.mOffset += mLayoutStyle.mControlPadding;
	layoutRect.mSize -= mLayoutStyle.mControlPadding * 2;

	mLayoutStack.push({ l.mCachedTransform, l.mScreenSpace, axis, layoutRect, layoutRect, 0, l.mLayoutDepth + 2*DEPTH_DELTA });

	return layoutRect;
}
AlignedBox2f GuiContext::BeginScrollSubLayout(float size, float contentSize) {
	GuiLayout& l = mLayoutStack.top();
	LayoutSpace(mLayoutStyle.mControlPadding);
	AlignedBox2f layoutRect = l.Get(size);
	LayoutSpace(mLayoutStyle.mControlPadding);

	uint32_t controlId = mNextControlId++;

	float scrollAmount = 0;
	if (mControlData.count(controlId)) {
		const auto& v = mControlData.at(controlId);
		if (v.index() == 0) scrollAmount = get<float>(v);
	}

	if (l.mScreenSpace) {
		if (MouseKeyboardInput* i = mInputManager->GetFirst<MouseKeyboardInput>()) {
			Vector2f c = i->CursorPos();
			c.y = i->WindowHeight() - c.y;
			if (layoutRect.Contains(c) && l.mClipRect.Contains(c)) {
				scrollAmount -= i->ScrollDelta()*10;
				const_cast<InputPointer*>(i->GetPointer(0))->mGuiHitT = 0.f;
			}
		}
	} else {
		Matrix4f invTransform = inverse(l.mCachedTransform);
		for (const InputSystem* d : mInputManager->InputDevices())
			for (uint32_t i = 0; i < d->PointerCount(); i++) {
				const InputPointer* p = d->GetPointer(i);

				fRay ray = p->mWorldRay;
				ray.mOrigin = (Vector3f)(invTransform * Vector4f(ray.mOrigin, 1));
				ray.mDirection = (Vector3f)(invTransform * Vector4f(ray.mDirection, 0));

				float t = ray.Intersect(Vector4f(0, 0, 1, l.mLayoutDepth));

				Vector2f c = (Vector2f)(ray.mOrigin + ray.mDirection * t);
				if (layoutRect.Contains(c) && l.mClipRect.Contains(c)) {
					scrollAmount -= p->mScrollDelta[l.mAxis == LayoutAxis::eHorizontal ? 0 : 1] * contentSize * .25f;
					const_cast<InputPointer*>(p)->mGuiHitT = t;
				}
			}
	}

	float scrollMax = fmaxf(0.f, contentSize - layoutRect.mSize.y);
	scrollAmount = clamp(scrollAmount, 0.f, scrollMax);

	mControlData[controlId] = scrollAmount;

	if (mLayoutStyle.mBackgroundColor.w > 0) {
		Vector4f c = mLayoutStyle.mBackgroundColor;
		c.rgb *= 0.75f;
		if (l.mScreenSpace)
			Rect(layoutRect, l.mLayoutDepth, c, nullptr, 0, l.mClipRect);
		else
			Rect(l.mCachedTransform * Matrix4f::Translate(Vector3f(0, 0, l.mLayoutDepth)), layoutRect, c, nullptr, 0, l.mClipRect);
	}

	AlignedBox2f contentRect = layoutRect;
	contentRect.mOffset += mLayoutStyle.mControlPadding;
	contentRect.mSize -= mLayoutStyle.mControlPadding * 2;
	switch (l.mAxis) {
	case LayoutAxis::eHorizontal:
		contentRect.mOffset.x -= scrollAmount + (layoutRect.mSize.x - contentSize);
		contentRect.mSize.x = contentSize - mLayoutStyle.mControlPadding * 2;
		break;
	case LayoutAxis::eVertical:
		contentRect.mOffset.y += (layoutRect.mSize.y - contentSize) + scrollAmount;
		contentRect.mSize.y = contentSize - mLayoutStyle.mControlPadding * 2;
		break;
	}
	
	// scroll bar slider
	if (scrollMax > 0) {
		AlignedBox2f slider;
		AlignedBox2f sliderbg;

		switch (l.mAxis) {
		case LayoutAxis::eHorizontal:
			slider.mSize = Vector2f(layoutRect.mSize.x * (layoutRect.mSize.x / contentSize), mLayoutStyle.mScrollBarThickness);
			slider.mOffset = layoutRect.mOffset + Vector2f((layoutRect.mSize.x - slider.mSize.x) * (scrollAmount / scrollMax), 0);
			sliderbg.mOffset = layoutRect.mOffset;
			sliderbg.mSize = Vector2f(layoutRect.mSize.x, slider.mSize.y);

			contentRect.mOffset.y += slider.mSize.y;
			layoutRect.mOffset.y += slider.mSize.y;
			layoutRect.mSize.y -= slider.mSize.y;
			break;

		case LayoutAxis::eVertical:
			slider.mSize = Vector2f(mLayoutStyle.mScrollBarThickness, layoutRect.mSize.y * (layoutRect.mSize.y / contentSize));
			slider.mOffset = layoutRect.mOffset + Vector2f(layoutRect.mSize.x - slider.mSize.x, (layoutRect.mSize.y - slider.mSize.y) * (1 - scrollAmount / scrollMax));
			sliderbg.mOffset = layoutRect.mOffset + Vector2f(layoutRect.mSize.x - slider.mSize.x, 0);
			sliderbg.mSize = Vector2f(slider.mSize.x, layoutRect.mSize.y);

			layoutRect.mSize.x -= slider.mSize.x;
			contentRect.mSize.x -= slider.mSize.x;
			break;
		}

		uint32_t scrollAxis = l.mAxis == LayoutAxis::eHorizontal ? 0 : 1;
		uint32_t otherAxis = l.mAxis == LayoutAxis::eHorizontal ? 1 : 0;

		Vector2f offset = slider.mOffset;
		float extent = slider.mSize[otherAxis];
		slider.mOffset[scrollAxis] += extent / 2;
		slider.mSize[scrollAxis] = fmaxf(0, slider.mSize[scrollAxis] - extent);
		
		if (l.mScreenSpace) {
			Rect(slider, l.mLayoutDepth + DEPTH_DELTA, mLayoutStyle.mSliderColor, nullptr, 0);
			Rect(slider, l.mLayoutDepth + 2*DEPTH_DELTA, mLayoutStyle.mSliderKnobColor, nullptr, 0);
			
			Rect(AlignedBox2f(offset, extent), l.mLayoutDepth + 2*DEPTH_DELTA, mLayoutStyle.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
			offset[scrollAxis] += floorf(slider.mSize[scrollAxis] - 0.5f);
			Rect(AlignedBox2f(offset, extent), l.mLayoutDepth + 2*DEPTH_DELTA, mLayoutStyle.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
		} else {
			Rect(l.mCachedTransform * Matrix4f::Translate(Vector3f(0, 0, l.mLayoutDepth + DEPTH_DELTA)), sliderbg, mLayoutStyle.mSliderColor);
			Rect(l.mCachedTransform * Matrix4f::Translate(Vector3f(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), slider, mLayoutStyle.mSliderKnobColor);
			
			Rect(l.mCachedTransform * Matrix4f::Translate(Vector3f(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), AlignedBox2f(offset, extent), mLayoutStyle.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
			offset[scrollAxis] += slider.mSize[scrollAxis];
			Rect(l.mCachedTransform * Matrix4f::Translate(Vector3f(0, 0, l.mLayoutDepth + 2*DEPTH_DELTA)), AlignedBox2f(offset, extent), mLayoutStyle.mSliderKnobColor, mIconsTexture, ICON_CIRCLE_ST, l.mClipRect);
		}
	}

	mLayoutStack.push({ l.mCachedTransform, l.mScreenSpace, l.mAxis, contentRect, layoutRect, 0, l.mLayoutDepth + 3*DEPTH_DELTA });

	return contentRect;
}