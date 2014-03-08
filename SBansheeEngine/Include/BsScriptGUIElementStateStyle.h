#pragma once

#include "BsScriptEnginePrerequisites.h"
#include "BsScriptObject.h"
#include "BsMonoClass.h"
#include "BsGUIElementStyle.h"
#include "BsScriptMacros.h"
#include "BsScriptSpriteTexture.h"

namespace BansheeEngine
{
	class BS_SCR_BE_EXPORT ScriptGUIElementStateStyle : public ScriptObject<ScriptGUIElementStateStyle>
	{
	public:
		~ScriptGUIElementStateStyle();

		static void initMetaData();
		GUIElementStyle::GUIElementStateStyle getInternalValue() const { return *mElementStateStyle; }

	private:
		static void internal_createInstance(MonoObject* instance);
		static void internal_createInstanceExternal(MonoObject* instance, GUIElementStyle::GUIElementStateStyle* externalStateStyle);
		static void internal_destroyInstance(ScriptGUIElementStateStyle* nativeInstance);

		static void initRuntimeData();

		CM_SCRIPT_GETSET_OBJECT_SHRDPTR(ScriptGUIElementStateStyle, ScriptSpriteTexture, Texture, mElementStateStyle->texture, mSpriteTexture);
		CM_SCRIPT_GETSET_VALUE_REF(ScriptGUIElementStateStyle, CM::Color, TextColor, mElementStateStyle->textColor);

		ScriptGUIElementStateStyle();
		ScriptGUIElementStateStyle(GUIElementStyle::GUIElementStateStyle* externalStyle);

		GUIElementStyle::GUIElementStateStyle* mElementStateStyle;
		ScriptSpriteTexture* mSpriteTexture;
		bool mOwnsStyle;
	};
}