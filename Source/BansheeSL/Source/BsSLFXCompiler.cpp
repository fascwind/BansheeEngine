//********************************** Banshee Engine (www.banshee3d.com) **************************************************//
//**************** Copyright (c) 2016 Marko Pintera (marko.pintera@gmail.com). All rights reserved. **********************//
#include "BsSLFXCompiler.h"
#include "BsGpuProgram.h"
#include <regex>
#include "BsShader.h"
#include "BsTechnique.h"
#include "BsPass.h"
#include "BsSamplerState.h"
#include "BsRenderAPI.h"
#include "BsDebug.h"
#include "BsShaderManager.h"
#include "BsShaderInclude.h"
#include "BsMatrix4.h"
#include "BsBuiltinResources.h"

#define XSC_ENABLE_LANGUAGE_EXT 1
#include "Xsc/Xsc.h"

extern "C" {
#include "BsMMAlloc.h"
#include "BsParserFX.h"
#include "BsLexerFX.h"
}

using namespace std;

namespace bs
{
	// Print out the FX AST, only for debug purposes
	void SLFXDebugPrint(ASTFXNode* node, String indent)
	{
		LOGDBG(indent + "NODE " + toString(node->type));

		for (int i = 0; i < node->options->count; i++)
		{
			OptionDataType odt = OPTION_LOOKUP[(int)node->options->entries[i].type].dataType;
			if (odt == ODT_Complex)
			{
				LOGDBG(indent + toString(i) + ". " + toString(node->options->entries[i].type));
				SLFXDebugPrint(node->options->entries[i].value.nodePtr, indent + "\t");
				continue;
			}

			String value;
			switch (odt)
			{
			case ODT_Bool:
				value = toString(node->options->entries[i].value.intValue != 0);
				break;
			case ODT_Int:
				value = toString(node->options->entries[i].value.intValue);
				break;
			case ODT_Float:
				value = toString(node->options->entries[i].value.floatValue);
				break;
			case ODT_String:
				value = node->options->entries[i].value.strValue;
				break;
			case ODT_Matrix:
			{
				Matrix4 mat4 = *(Matrix4*)(node->options->entries[i].value.matrixValue);
				value = toString(mat4);
			}
				break;
			default:
				break;
			}

			LOGDBG(indent + toString(i) + ". " + toString(node->options->entries[i].type) + " = " + value);
		}
	}

	class XscLog : public Xsc::Log
	{
	public:
		void SubmitReport(const Xsc::Report& report) override
		{
			switch (report.Type())
			{
			case Xsc::ReportTypes::Info:
				mInfos.push_back({ FullIndent(), report });
				break;
			case Xsc::ReportTypes::Warning:
				mWarnings.push_back({ FullIndent(), report });
				break;
			case Xsc::ReportTypes::Error:
				mErrors.push_back({ FullIndent(), report });
				break;
			}
		}

		void getMessages(StringStream& output)
		{
			printAndClearReports(output, mInfos);
			printAndClearReports(output, mWarnings, (mWarnings.size() == 1 ? "WARNING" : "WARNINGS"));
			printAndClearReports(output, mErrors, (mErrors.size() == 1 ? "ERROR" : "ERRORS"));
		}

	private:
		struct IndentReport
		{
			std::string indent;
			Xsc::Report      report;
		};

		static void printMultiLineString(StringStream& output, const std::string& str, const std::string& indent)
		{
			// Determine at which position the actual text begins (excluding the "error (X:Y) : " or the like) 
			auto textStartPos = str.find(" : ");
			if (textStartPos != std::string::npos)
				textStartPos += 3;
			else
				textStartPos = 0;

			std::string newLineIndent(textStartPos, ' ');

			size_t start = 0;
			bool useNewLineIndent = false;
			while (start < str.size())
			{
				output << indent;

				if (useNewLineIndent)
					output << newLineIndent;

				// Print next line
				auto end = str.find('\n', start);

				if (end != std::string::npos)
				{
					output << str.substr(start, end - start);
					start = end + 1;
				}
				else
				{
					output << str.substr(start);
					start = end;
				}

				output << std::endl;
				useNewLineIndent = true;
			}
		}

		void printReport(StringStream& output, const IndentReport& r)
		{
			// Print optional context description
			if (!r.report.Context().empty())
				printMultiLineString(output, r.report.Context(), r.indent);

			// Print report message
			const auto& msg = r.report.Message();
			printMultiLineString(output, msg, r.indent);

			// Print optional line and line-marker
			if (r.report.HasLine())
			{
				const auto& line = r.report.Line();
				const auto& marker = r.report.Marker();

				// Print line
				output << r.indent << line << std::endl;

				// Print line marker
				output << r.indent << marker << std::endl;
			}

			// Print optional hints
			for (const auto& hint : r.report.GetHints())
				output << r.indent << hint << std::endl;
		}

		void printAndClearReports(StringStream& output, Vector<IndentReport>& reports, const String& headline = "")
		{
			if (!reports.empty())
			{
				if (!headline.empty())
				{
					String s = toString(reports.size()) + " " + headline;
					output << s << std::endl;
					output << String(s.size(), '-') << std::endl;
				}

				for (const auto& r : reports)
					printReport(output, r);

				reports.clear();
			}
		}

		Vector<IndentReport> mInfos;
		Vector<IndentReport> mWarnings;
		Vector<IndentReport> mErrors;

	};

	GpuParamObjectType ReflTypeToTextureType(Xsc::Reflection::BufferType type)
	{
		switch(type)
		{
		case Xsc::Reflection::BufferType::RWTexture1D: return GPOT_RWTEXTURE1D;
		case Xsc::Reflection::BufferType::RWTexture1DArray: return GPOT_RWTEXTURE1DARRAY;
		case Xsc::Reflection::BufferType::RWTexture2D: return GPOT_RWTEXTURE2D;
		case Xsc::Reflection::BufferType::RWTexture2DArray: return GPOT_RWTEXTURE2DARRAY;
		case Xsc::Reflection::BufferType::RWTexture3D: return GPOT_RWTEXTURE3D;
		case Xsc::Reflection::BufferType::Texture1D: return GPOT_TEXTURE1D;
		case Xsc::Reflection::BufferType::Texture1DArray: return GPOT_TEXTURE1DARRAY;
		case Xsc::Reflection::BufferType::Texture2D: return GPOT_TEXTURE2D;
		case Xsc::Reflection::BufferType::Texture2DArray: return GPOT_TEXTURE2DARRAY;
		case Xsc::Reflection::BufferType::Texture3D: return GPOT_TEXTURE3D;
		case Xsc::Reflection::BufferType::TextureCube: return GPOT_TEXTURECUBE;
		case Xsc::Reflection::BufferType::TextureCubeArray: return GPOT_TEXTURECUBEARRAY;
		case Xsc::Reflection::BufferType::Texture2DMS: return GPOT_TEXTURE2DMS;
		case Xsc::Reflection::BufferType::Texture2DMSArray: return GPOT_TEXTURE2DMSARRAY;
		default: return GPOT_UNKNOWN;
		}
	}

	GpuParamObjectType ReflTypeToBufferType(Xsc::Reflection::BufferType type)
	{
		switch(type)
		{
		case Xsc::Reflection::BufferType::Buffer: return GPOT_RWTYPED_BUFFER;
		case Xsc::Reflection::BufferType::StructuredBuffer: return GPOT_STRUCTURED_BUFFER;
		case Xsc::Reflection::BufferType::ByteAddressBuffer: return GPOT_BYTE_BUFFER;
		case Xsc::Reflection::BufferType::RWBuffer: return GPOT_RWTYPED_BUFFER;
		case Xsc::Reflection::BufferType::RWStructuredBuffer: return GPOT_RWSTRUCTURED_BUFFER;
		case Xsc::Reflection::BufferType::RWByteAddressBuffer: return GPOT_RWBYTE_BUFFER;
		case Xsc::Reflection::BufferType::AppendStructuredBuffer: return GPOT_RWAPPEND_BUFFER;
		case Xsc::Reflection::BufferType::ConsumeStructuredBuffer: return GPOT_RWCONSUME_BUFFER;
		default: return GPOT_UNKNOWN;
		}
	}

	GpuParamDataType ReflTypeToDataType(Xsc::Reflection::DataType type)
	{
		switch (type)
		{
		case Xsc::Reflection::DataType::Bool: return GPDT_BOOL;
		case Xsc::Reflection::DataType::Float: return GPDT_FLOAT1;
		case Xsc::Reflection::DataType::Float2: return GPDT_FLOAT2;
		case Xsc::Reflection::DataType::Float3: return GPDT_FLOAT3;
		case Xsc::Reflection::DataType::Float4: return GPDT_FLOAT4;
		case Xsc::Reflection::DataType::UInt:
		case Xsc::Reflection::DataType::Int:
			return GPDT_INT1;
		case Xsc::Reflection::DataType::UInt2:
		case Xsc::Reflection::DataType::Int2:
			return GPDT_INT2;
		case Xsc::Reflection::DataType::UInt3:
		case Xsc::Reflection::DataType::Int3: 
			return GPDT_INT3;
		case Xsc::Reflection::DataType::UInt4:
		case Xsc::Reflection::DataType::Int4: 
			return GPDT_INT4;
		case Xsc::Reflection::DataType::Float2x2: return GPDT_MATRIX_2X2;
		case Xsc::Reflection::DataType::Float2x3: return GPDT_MATRIX_2X3;
		case Xsc::Reflection::DataType::Float2x4: return GPDT_MATRIX_2X4;
		case Xsc::Reflection::DataType::Float3x2: return GPDT_MATRIX_3X4;
		case Xsc::Reflection::DataType::Float3x3: return GPDT_MATRIX_3X3;
		case Xsc::Reflection::DataType::Float3x4: return GPDT_MATRIX_3X4;
		case Xsc::Reflection::DataType::Float4x2: return GPDT_MATRIX_4X2;
		case Xsc::Reflection::DataType::Float4x3: return GPDT_MATRIX_4X3;
		case Xsc::Reflection::DataType::Float4x4: return GPDT_MATRIX_4X4;
		default: return GPDT_UNKNOWN;
		}
	}

	HTexture getBuiltinTexture(UINT32 idx)
	{
		if (idx == 1)
			return BuiltinResources::getTexture(BuiltinTexture::White);
		else if (idx == 2)
			return BuiltinResources::getTexture(BuiltinTexture::Black);
		else if (idx == 3)
			return BuiltinResources::getTexture(BuiltinTexture::Normal);

		return HTexture();
	}

	TextureAddressingMode parseTexAddrMode(Xsc::Reflection::TextureAddressMode addrMode)
	{
		switch (addrMode)
		{
		case Xsc::Reflection::TextureAddressMode::Border:
			return TAM_BORDER;
		case Xsc::Reflection::TextureAddressMode::Clamp:
			return TAM_CLAMP;
		case Xsc::Reflection::TextureAddressMode::Mirror:
		case Xsc::Reflection::TextureAddressMode::MirrorOnce:
			return TAM_MIRROR;
		case Xsc::Reflection::TextureAddressMode::Wrap:
		default:
			return TAM_WRAP;
		}
	}

	CompareFunction parseCompFunction(Xsc::Reflection::ComparisonFunc compFunc)
	{
		switch(compFunc)
		{
		case Xsc::Reflection::ComparisonFunc::Always:
		default:
			return CMPF_ALWAYS_PASS;
		case Xsc::Reflection::ComparisonFunc::Never:
			return CMPF_ALWAYS_FAIL;
		case Xsc::Reflection::ComparisonFunc::Equal:
			return CMPF_EQUAL;
		case Xsc::Reflection::ComparisonFunc::Greater:
			return CMPF_GREATER;
		case Xsc::Reflection::ComparisonFunc::GreaterEqual:
			return CMPF_GREATER_EQUAL;
		case Xsc::Reflection::ComparisonFunc::Less:
			return CMPF_LESS;
		case Xsc::Reflection::ComparisonFunc::LessEqual:
			return CMPF_LESS_EQUAL;
		case Xsc::Reflection::ComparisonFunc::NotEqual:
			return CMPF_NOT_EQUAL;
		}
	}

	SPtr<SamplerState> parseSamplerState(const Xsc::Reflection::SamplerState& sampState)
	{
		SAMPLER_STATE_DESC desc;

		desc.addressMode.u = parseTexAddrMode(sampState.addressU);
		desc.addressMode.v = parseTexAddrMode(sampState.addressV);
		desc.addressMode.w = parseTexAddrMode(sampState.addressW);

		desc.borderColor[0] = sampState.borderColor[0];
		desc.borderColor[1] = sampState.borderColor[1];
		desc.borderColor[2] = sampState.borderColor[2];
		desc.borderColor[3] = sampState.borderColor[3];

		desc.comparisonFunc = parseCompFunction(sampState.comparisonFunc);
		desc.maxAniso = sampState.maxAnisotropy;
		desc.mipMax = sampState.maxLOD;
		desc.mipMin = sampState.minLOD;
		desc.mipmapBias = sampState.mipLODBias;

		switch (sampState.filter)
		{
		case Xsc::Reflection::Filter::MinMagMipPoint: 
		case Xsc::Reflection::Filter::ComparisonMinMagMipPoint: 
			desc.minFilter = FO_POINT;
			desc.magFilter = FO_POINT;
			desc.mipFilter = FO_POINT;
			break;
		case Xsc::Reflection::Filter::MinMagPointMipLinear: 
		case Xsc::Reflection::Filter::ComparisonMinMagPointMipLinear: 
			desc.minFilter = FO_POINT;
			desc.magFilter = FO_POINT;
			desc.mipFilter = FO_LINEAR;
			break;
		case Xsc::Reflection::Filter::MinPointMagLinearMipPoint: 
		case Xsc::Reflection::Filter::ComparisonMinPointMagLinearMipPoint: 
			desc.minFilter = FO_POINT;
			desc.magFilter = FO_LINEAR;
			desc.mipFilter = FO_POINT;
			break;
		case Xsc::Reflection::Filter::MinPointMagMipLinear: 
		case Xsc::Reflection::Filter::ComparisonMinPointMagMipLinear: 
			desc.minFilter = FO_POINT;
			desc.magFilter = FO_LINEAR;
			desc.mipFilter = FO_LINEAR;
			break;
		case Xsc::Reflection::Filter::MinLinearMagMipPoint: 
		case Xsc::Reflection::Filter::ComparisonMinLinearMagMipPoint: 
			desc.minFilter = FO_LINEAR;
			desc.magFilter = FO_POINT;
			desc.mipFilter = FO_POINT;
			break;
		case Xsc::Reflection::Filter::MinLinearMagPointMipLinear: 
		case Xsc::Reflection::Filter::ComparisonMinLinearMagPointMipLinear: 
			desc.minFilter = FO_LINEAR;
			desc.magFilter = FO_POINT;
			desc.mipFilter = FO_LINEAR;
			break;
		case Xsc::Reflection::Filter::MinMagLinearMipPoint: 
		case Xsc::Reflection::Filter::ComparisonMinMagLinearMipPoint: 
			desc.minFilter = FO_LINEAR;
			desc.magFilter = FO_LINEAR;
			desc.mipFilter = FO_POINT;
			break;
		case Xsc::Reflection::Filter::MinMagMipLinear: 
		case Xsc::Reflection::Filter::ComparisonMinMagMipLinear: 
			desc.minFilter = FO_LINEAR;
			desc.magFilter = FO_LINEAR;
			desc.mipFilter = FO_LINEAR;
			break;
		case Xsc::Reflection::Filter::Anisotropic: 
		case Xsc::Reflection::Filter::ComparisonAnisotropic: 
			desc.minFilter = FO_ANISOTROPIC;
			desc.magFilter = FO_ANISOTROPIC;
			desc.minFilter = FO_ANISOTROPIC;
			break;
		default: 
			break;
		}
		
		return SamplerState::create(desc);
	}

	void parseParameters(const Xsc::Reflection::ReflectionData& reflData, SHADER_DESC& desc)
	{
		for(auto& entry : reflData.uniforms)
		{
			if ((entry.flags & Xsc::Reflection::Uniform::Flags::Internal) != 0)
				continue;

			String ident = entry.ident.c_str();
			switch(entry.type)
			{
			case Xsc::Reflection::UniformType::UniformBuffer:
				desc.setParamBlockAttribs(entry.ident.c_str(), false, GPBU_STATIC);
				break;
			case Xsc::Reflection::UniformType::Buffer:
				{
					GpuParamObjectType objType = ReflTypeToTextureType((Xsc::Reflection::BufferType)entry.baseType);
					if(objType != GPOT_UNKNOWN)
					{
						if (entry.defaultValue == -1)
							desc.addParameter(ident, ident, objType);
						else
							desc.addParameter(ident, ident, objType, getBuiltinTexture(entry.defaultValue));
					}
					else
					{
						objType = ReflTypeToBufferType((Xsc::Reflection::BufferType)entry.baseType);
						desc.addParameter(ident, ident, objType);
					}
				}
				break;
			case Xsc::Reflection::UniformType::Sampler: 
			{
				auto findIter = reflData.samplerStates.find(entry.ident);
				if (findIter == reflData.samplerStates.end() || !findIter->second.isNonDefault)
					desc.addParameter(ident, ident, GPOT_SAMPLER2D);
				else
				{
					SPtr<SamplerState> defaultVal = parseSamplerState(findIter->second);
					desc.addParameter(ident, ident, GPOT_SAMPLER2D, defaultVal);
				}
				break;
			}
			case Xsc::Reflection::UniformType::Variable: 
			{
				bool isBlockInternal = false;
				if(entry.uniformBlock != -1)
				{
					std::string blockName = reflData.constantBuffers[entry.uniformBlock].ident;
					for (auto& uniform : reflData.uniforms)
					{
						if (uniform.type == Xsc::Reflection::UniformType::UniformBuffer && uniform.ident == blockName)
						{
							isBlockInternal = (uniform.flags & Xsc::Reflection::Uniform::Flags::Internal) != 0;
							break;
						}
					}
				}

				if (!isBlockInternal)
				{
					GpuParamDataType type = ReflTypeToDataType((Xsc::Reflection::DataType)entry.baseType);
					if ((entry.flags & Xsc::Reflection::Uniform::Flags::Color) != 0 &&
						(type == GPDT_FLOAT3 || type == GPDT_FLOAT4))
					{
						type = GPDT_COLOR;
					}

					if (entry.defaultValue == -1)
						desc.addParameter(ident, ident, type);
					else
					{
						const Xsc::Reflection::DefaultValue& defVal = reflData.defaultValues[entry.defaultValue];

						desc.addParameter(ident, ident, type, StringID::NONE, 1, 0, (UINT8*)defVal.matrix);
					}
				}
			}
				break;
			case Xsc::Reflection::UniformType::Struct: break;
			default: ;
			}
		}
	}

	String crossCompile(const String& hlsl, GpuProgramType type, bool vulkan, bool optionalEntry, UINT32& startBindingSlot,
		SHADER_DESC* shaderDesc = nullptr, Vector<GpuProgramType>* detectedTypes = nullptr)
	{
		SPtr<StringStream> input = bs_shared_ptr_new<StringStream>();

		if (vulkan)
			*input << "#define VULKAN 1" << std::endl;
		else
			*input << "#define OPENGL 1" << std::endl;

		*input << hlsl;

		Xsc::ShaderInput inputDesc;
		inputDesc.shaderVersion = Xsc::InputShaderVersion::HLSL5;
		inputDesc.sourceCode = input;
		inputDesc.extensions = Xsc::Extensions::LayoutAttribute;

		switch (type)
		{
		case GPT_VERTEX_PROGRAM:
			inputDesc.shaderTarget = Xsc::ShaderTarget::VertexShader;
			inputDesc.entryPoint = "vsmain";
			break;
		case GPT_GEOMETRY_PROGRAM:
			inputDesc.shaderTarget = Xsc::ShaderTarget::GeometryShader;
			inputDesc.entryPoint = "gsmain";
			break;
		case GPT_HULL_PROGRAM:
			inputDesc.shaderTarget = Xsc::ShaderTarget::TessellationControlShader;
			inputDesc.entryPoint = "hsmain";
			break;
		case GPT_DOMAIN_PROGRAM:
			inputDesc.shaderTarget = Xsc::ShaderTarget::TessellationEvaluationShader;
			inputDesc.entryPoint = "dsmain";
			break;
		case GPT_FRAGMENT_PROGRAM:
			inputDesc.shaderTarget = Xsc::ShaderTarget::FragmentShader;
			inputDesc.entryPoint = "fsmain";
			break;
		case GPT_COMPUTE_PROGRAM:
			inputDesc.shaderTarget = Xsc::ShaderTarget::ComputeShader;
			inputDesc.entryPoint = "csmain";
			break;
		}

		StringStream output;

		Xsc::ShaderOutput outputDesc;
		outputDesc.sourceCode = &output;
		outputDesc.options.autoBinding = vulkan;
		outputDesc.options.autoBindingStartSlot = startBindingSlot;
		outputDesc.options.separateShaders = true;
		outputDesc.options.separateSamplers = false;
		outputDesc.nameMangling.inputPrefix = "bs_";
		outputDesc.nameMangling.useAlwaysSemantics = true;
		outputDesc.nameMangling.renameBufferFields = true;

		if (vulkan)
			outputDesc.shaderVersion = Xsc::OutputShaderVersion::VKSL450;
		else
			outputDesc.shaderVersion = Xsc::OutputShaderVersion::GLSL450;

		XscLog log;
		Xsc::Reflection::ReflectionData reflectionData;
		bool compileSuccess = Xsc::CompileShader(inputDesc, outputDesc, &log, &reflectionData);
		if (!compileSuccess)
		{
			// If enabled, don't fail if entry point isn't found
			bool done = true;
			if(optionalEntry)
			{
				bool entryFound = false;
				for (auto& entry : reflectionData.functions)
				{
					if(entry.ident == inputDesc.entryPoint)
					{
						entryFound = true;
						break;
					}
				}

				if (!entryFound)
					done = false;
			}

			if (done)
			{
				StringStream logOutput;
				log.getMessages(logOutput);

				LOGERR("Shader cross compilation failed. Log: \n\n" + logOutput.str());
				return "";
			}
		}

		for (auto& entry : reflectionData.constantBuffers)
			startBindingSlot = std::max(startBindingSlot, entry.location + 1u);

		for (auto& entry : reflectionData.textures)
			startBindingSlot = std::max(startBindingSlot, entry.location + 1u);

		for (auto& entry : reflectionData.storageBuffers)
			startBindingSlot = std::max(startBindingSlot, entry.location + 1u);

		if(detectedTypes != nullptr)
		{
			for(auto& entry : reflectionData.functions)
			{
				if (entry.ident == "vsmain")
					detectedTypes->push_back(GPT_VERTEX_PROGRAM);
				else if (entry.ident == "fsmain")
					detectedTypes->push_back(GPT_FRAGMENT_PROGRAM);
				else if (entry.ident == "gsmain")
					detectedTypes->push_back(GPT_GEOMETRY_PROGRAM);
				else if (entry.ident == "dsmain")
					detectedTypes->push_back(GPT_DOMAIN_PROGRAM);
				else if (entry.ident == "hsmain")
					detectedTypes->push_back(GPT_HULL_PROGRAM);
				else if (entry.ident == "csmain")
					detectedTypes->push_back(GPT_COMPUTE_PROGRAM);
			}

			// If no entry points found, and error occurred, report error
			if(!compileSuccess && detectedTypes->size() == 0)
			{
				StringStream logOutput;
				log.getMessages(logOutput);

				LOGERR("Shader cross compilation failed. Log: \n\n" + logOutput.str());
				return "";
			}
		}

		if (shaderDesc != nullptr)
			parseParameters(reflectionData, *shaderDesc);

		return output.str();
	}

	// Convert HLSL code to GLSL
	String HLSLtoGLSL(const String& hlsl, GpuProgramType type, bool vulkan, UINT32& startBindingSlot)
	{
		return crossCompile(hlsl, type, vulkan, false, startBindingSlot);
	}

	void reflectHLSL(const String& hlsl, SHADER_DESC& shaderDesc, Vector<GpuProgramType>& entryPoints)
	{
		UINT32 dummy = 0;
		crossCompile(hlsl, GPT_VERTEX_PROGRAM, false, true, dummy, &shaderDesc, &entryPoints);
	}

	BSLFXCompileResult BSLFXCompiler::compile(const String& name, const String& source, 
		const UnorderedMap<String, String>& defines)
	{
		BSLFXCompileResult output;

		String parsedSource = source;

		ParseState* parseState = parseStateCreate();
		for(auto& define : defines)
		{
			if (define.first.size() == 0)
				continue;

			addDefine(parseState, define.first.c_str());

			if(define.second.size() > 0)
				addDefineExpr(parseState, define.second.c_str());
		}

		parseFX(parseState, parsedSource.c_str());

		if (parseState->hasError > 0)
		{
			output.errorMessage = parseState->errorMessage;
			output.errorLine = parseState->errorLine;
			output.errorColumn = parseState->errorColumn;

			if(parseState->errorFile != nullptr)
				output.errorFile = parseState->errorFile;

			parseStateDelete(parseState);
		}
		else
		{
			// Only enable for debug purposes
			//SLFXDebugPrint(parseState->rootNode, "");

			Vector<String> codeBlocks;
			CodeString* codeString = parseState->codeStrings;
			while(codeString != nullptr)
			{
				while ((INT32)codeBlocks.size() <= codeString->index)
					codeBlocks.push_back(String());

				codeBlocks[codeString->index] = String(codeString->code, codeString->size);
				codeString = codeString->next;
			}

			output = parseShader(name, parseState, codeBlocks);

			StringStream gpuProgError;
			bool hasError = false;
			if (output.shader != nullptr)
			{
				Vector<SPtr<Technique>> techniques = output.shader->getCompatibleTechniques();

				for (auto& technique : techniques)
				{
					UINT32 numPasses = technique->getNumPasses();

					for (UINT32 i = 0; i < numPasses; i++)
					{
						SPtr<Pass> pass = technique->getPass(i);

						auto checkCompileStatus = [&](const String& prefix, const SPtr<GpuProgram>& prog)
						{
							if (prog != nullptr)
							{
								prog->blockUntilCoreInitialized();

								if (!prog->isCompiled())
								{
									hasError = true;
									gpuProgError << prefix << ": " << prog->getCompileErrorMessage() << std::endl;
								}
							}
						};

						checkCompileStatus("Vertex program", pass->getVertexProgram());
						checkCompileStatus("Fragment program", pass->getFragmentProgram());
						checkCompileStatus("Geometry program", pass->getGeometryProgram());
						checkCompileStatus("Hull program", pass->getHullProgram());
						checkCompileStatus("Domain program", pass->getDomainProgram());
						checkCompileStatus("Compute program", pass->getComputeProgram());
					}
				}
			}

			if (hasError)
			{
				output.errorMessage = "Failed compiling GPU program(s): " + gpuProgError.str();
				output.errorLine = 0;
				output.errorColumn = 0;
			}
		}

		return output;
	}

	void BSLFXCompiler::parseFX(ParseState* parseState, const char* source)
	{
		yyscan_t scanner;
		YY_BUFFER_STATE state;

		if (yylex_init_extra(parseState, &scanner))
			return;

		// If debug output from lexer is needed uncomment this and add %debug option to lexer file
		//yyset_debug(true, scanner);

		// If debug output from parser is needed uncomment this and add %debug option to parser file
		//yydebug = true;

		state = yy_scan_string(source, scanner);

		if (yyparse(parseState, scanner))
			return;

		yy_delete_buffer(state, scanner);
		yylex_destroy(scanner);
	}

	BSLFXCompiler::TechniqueMetaData BSLFXCompiler::parseTechniqueMetaData(ASTFXNode* technique)
	{
		TechniqueMetaData metaData;

		metaData.renderer = RendererAny;
		metaData.language = "hlsl";
		metaData.isMixin = technique->type == NT_Mixin;

		for (int i = 0; i < technique->options->count; i++)
		{
			NodeOption* option = &technique->options->entries[i];

			switch (option->type)
			{
			case OT_Renderer:
				metaData.renderer = parseRenderer(removeQuotes(option->value.strValue));
				break;
			case OT_Tags:
			{
				ASTFXNode* tagsNode = option->value.nodePtr;
				for (int j = 0; j < tagsNode->options->count; j++)
				{
					NodeOption* tagOption = &tagsNode->options->entries[j];

					if (tagOption->type == OT_TagValue)
						metaData.tags.push_back(removeQuotes(tagOption->value.strValue));
				}
			}
				break;
			case OT_Identifier:
				metaData.name = option->value.strValue;
				break;
			case OT_Mixin:
				metaData.includes.push_back(option->value.strValue);
				break;
			default:
				break;
			}
		}

		return metaData;
	}

	StringID BSLFXCompiler::parseRenderer(const String& name)
	{
		if (name == "Any")
			return RendererAny;
		else if (name == "Default")
			return RendererDefault;

		return RendererAny;
	}

	QueueSortType BSLFXCompiler::parseSortType(CullAndSortModeValue sortType)
	{
		switch (sortType)
		{
		case CASV_BackToFront:
			return QueueSortType::BackToFront;
		case CASV_FrontToBack:
			return QueueSortType::FrontToBack;
		case CASV_None:
			return QueueSortType::None;
		}

		return QueueSortType::None;
	}

	CompareFunction BSLFXCompiler::parseCompFunc(CompFuncValue compFunc)
	{
		switch (compFunc)
		{
		case CFV_Pass:
			return CMPF_ALWAYS_PASS;
		case CFV_Fail:
			return CMPF_ALWAYS_FAIL;
		case CFV_LT:
			return CMPF_LESS;
		case CFV_LTE:
			return CMPF_LESS_EQUAL;
		case CFV_EQ:
			return CMPF_EQUAL;
		case CFV_NEQ:
			return CMPF_NOT_EQUAL;
		case CFV_GT:
			return CMPF_GREATER;
		case CFV_GTE:
			return CMPF_GREATER_EQUAL;
		}

		return CMPF_ALWAYS_PASS;
	}

	BlendFactor BSLFXCompiler::parseBlendFactor(OpValue factor)
	{
		switch (factor)
		{
		case OV_One:
			return BF_ONE;
		case OV_Zero:
			return BF_ZERO;
		case OV_DestColor:
			return BF_DEST_COLOR;
		case OV_SrcColor:
			return BF_SOURCE_COLOR;
		case OV_InvDestColor:
			return BF_INV_DEST_COLOR;
		case OV_InvSrcColor:
			return BF_INV_SOURCE_COLOR;
		case OV_DestAlpha:
			return BF_DEST_ALPHA;
		case OV_SrcAlpha:
			return BF_SOURCE_ALPHA;
		case OV_InvDestAlpha:
			return BF_INV_DEST_ALPHA;
		case OV_InvSrcAlpha:
			return BF_INV_SOURCE_ALPHA;
		default:
			break;
		}

		return BF_ONE;
	}

	BlendOperation BSLFXCompiler::parseBlendOp(BlendOpValue op)
	{
		switch (op)
		{
		case BOV_Add:
			return BO_ADD;
		case BOV_Max:
			return BO_MAX;
		case BOV_Min:
			return BO_MIN;
		case BOV_Subtract:
			return BO_SUBTRACT;
		case BOV_RevSubtract:
			return BO_REVERSE_SUBTRACT;
		}

		return BO_ADD;
	}

	StencilOperation BSLFXCompiler::parseStencilOp(OpValue op)
	{
		switch (op)
		{
		case OV_Keep:
			return SOP_KEEP;
		case OV_Zero:
			return SOP_ZERO;
		case OV_Replace:
			return SOP_REPLACE;
		case OV_Incr:
			return SOP_INCREMENT;
		case OV_Decr:
			return SOP_DECREMENT;
		case OV_IncrWrap:
			return SOP_INCREMENT_WRAP;
		case OV_DecrWrap:
			return SOP_DECREMENT_WRAP;
		case OV_Invert:
			return SOP_INVERT;
		default:
			break;
		}

		return SOP_KEEP;
	}

	CullingMode BSLFXCompiler::parseCullMode(CullAndSortModeValue cm)
	{
		switch (cm)
		{
		case CASV_None:
			return CULL_NONE;
		case CASV_CW:
			return CULL_CLOCKWISE;
		case CASV_CCW:
			return CULL_COUNTERCLOCKWISE;
		}

		return CULL_COUNTERCLOCKWISE;
	}

	PolygonMode BSLFXCompiler::parseFillMode(FillModeValue fm)
	{
		if (fm == FMV_Wire)
			return PM_WIREFRAME;

		return PM_SOLID;
	}

	void BSLFXCompiler::parseStencilFront(DEPTH_STENCIL_STATE_DESC& desc, ASTFXNode* stencilOpNode)
	{
		if (stencilOpNode == nullptr || stencilOpNode->type != NT_StencilOp)
			return;

		for (int i = 0; i < stencilOpNode->options->count; i++)
		{
			NodeOption* option = &stencilOpNode->options->entries[i];

			switch (option->type)
			{
			case OT_Fail:
				desc.frontStencilFailOp = parseStencilOp((OpValue)option->value.intValue);
				break;
			case OT_ZFail:
				desc.frontStencilZFailOp = parseStencilOp((OpValue)option->value.intValue);
				break;
			case OT_PassOp:
				desc.frontStencilPassOp = parseStencilOp((OpValue)option->value.intValue);
				break;
			case OT_CompareFunc:
				desc.frontStencilComparisonFunc = parseCompFunc((CompFuncValue)option->value.intValue);
				break;
			default:
				break;
			}
		}
	}

	void BSLFXCompiler::parseStencilBack(DEPTH_STENCIL_STATE_DESC& desc, ASTFXNode* stencilOpNode)
	{
		if (stencilOpNode == nullptr || stencilOpNode->type != NT_StencilOp)
			return;

		for (int i = 0; i < stencilOpNode->options->count; i++)
		{
			NodeOption* option = &stencilOpNode->options->entries[i];

			switch (option->type)
			{
			case OT_Fail:
				desc.backStencilFailOp = parseStencilOp((OpValue)option->value.intValue);
				break;
			case OT_ZFail:
				desc.backStencilZFailOp = parseStencilOp((OpValue)option->value.intValue);
				break;
			case OT_PassOp:
				desc.backStencilPassOp = parseStencilOp((OpValue)option->value.intValue);
				break;
			case OT_CompareFunc:
				desc.backStencilComparisonFunc = parseCompFunc((CompFuncValue)option->value.intValue);
				break;
			default:
				break;
			}
		}
	}

	void BSLFXCompiler::parseColorBlendDef(RENDER_TARGET_BLEND_STATE_DESC& desc, ASTFXNode* blendDefNode)
	{
		if (blendDefNode == nullptr || blendDefNode->type != NT_BlendDef)
			return;

		for (int i = 0; i < blendDefNode->options->count; i++)
		{
			NodeOption* option = &blendDefNode->options->entries[i];

			switch (option->type)
			{
			case OT_Source:
				desc.srcBlend = parseBlendFactor((OpValue)option->value.intValue);
				break;
			case OT_Dest:
				desc.dstBlend = parseBlendFactor((OpValue)option->value.intValue);
				break;
			case OT_Op:
				desc.blendOp = parseBlendOp((BlendOpValue)option->value.intValue);
				break;
			default:
				break;
			}
		}
	}

	void BSLFXCompiler::parseAlphaBlendDef(RENDER_TARGET_BLEND_STATE_DESC& desc, ASTFXNode* blendDefNode)
	{
		if (blendDefNode == nullptr || blendDefNode->type != NT_BlendDef)
			return;

		for (int i = 0; i < blendDefNode->options->count; i++)
		{
			NodeOption* option = &blendDefNode->options->entries[i];

			switch (option->type)
			{
			case OT_Source:
				desc.srcBlendAlpha = parseBlendFactor((OpValue)option->value.intValue);
				break;
			case OT_Dest:
				desc.dstBlendAlpha = parseBlendFactor((OpValue)option->value.intValue);
				break;
			case OT_Op:
				desc.blendOpAlpha = parseBlendOp((BlendOpValue)option->value.intValue);
				break;
			default:
				break;
			}
		}
	}

	void BSLFXCompiler::parseRenderTargetBlendState(BLEND_STATE_DESC& desc, ASTFXNode* targetNode)
	{
		if (targetNode == nullptr || targetNode->type != NT_Target)
			return;

		UINT32 index = 0;

		for (int i = 0; i < targetNode->options->count; i++)
		{
			NodeOption* option = &targetNode->options->entries[i];

			switch (option->type)
			{
			case OT_Index:
				index = option->value.intValue;
				break;
			default:
				break;
			}
		}

		if (index >= BS_MAX_MULTIPLE_RENDER_TARGETS)
			return;

		RENDER_TARGET_BLEND_STATE_DESC& rtDesc = desc.renderTargetDesc[index];
		for (int i = 0; i < targetNode->options->count; i++)
		{
			NodeOption* option = &targetNode->options->entries[i];

			switch (option->type)
			{
			case OT_Enabled:
				rtDesc.blendEnable = option->value.intValue > 0;
				break;
			case OT_Color:
				parseColorBlendDef(rtDesc, option->value.nodePtr);
				break;
			case OT_Alpha:
				parseAlphaBlendDef(rtDesc, option->value.nodePtr);
				break;
			case OT_WriteMask:
				rtDesc.renderTargetWriteMask = option->value.intValue;
				break;
			default:
				break;
			}
		}
	}

	bool BSLFXCompiler::parseBlendState(PassData& desc, ASTFXNode* blendNode)
	{
		if (blendNode == nullptr || blendNode->type != NT_Blend)
			return false;

		bool isDefault = true;

		for (int i = 0; i < blendNode->options->count; i++)
		{
			NodeOption* option = &blendNode->options->entries[i];

			switch (option->type)
			{
			case OT_AlphaToCoverage:
				desc.blendDesc.alphaToCoverageEnable = option->value.intValue > 0;
				isDefault = false;
				break;
			case OT_IndependantBlend:
				desc.blendDesc.independantBlendEnable = option->value.intValue > 0;
				isDefault = false;
				break;
			case OT_Target:
				parseRenderTargetBlendState(desc.blendDesc, option->value.nodePtr);
				isDefault = false;
				break;
			default:
				break;
			}
		}

		return !isDefault;
	}

	bool BSLFXCompiler::parseRasterizerState(PassData& desc, ASTFXNode* rasterNode)
	{
		if (rasterNode == nullptr || rasterNode->type != NT_Raster)
			return false;

		bool isDefault = true;

		for (int i = 0; i < rasterNode->options->count; i++)
		{
			NodeOption* option = &rasterNode->options->entries[i];

			switch (option->type)
			{
			case OT_FillMode:
				desc.rasterizerDesc.polygonMode = parseFillMode((FillModeValue)option->value.intValue);
				isDefault = false;
				break;
			case OT_CullMode:
				desc.rasterizerDesc.cullMode = parseCullMode((CullAndSortModeValue)option->value.intValue);
				isDefault = false;
				break;
			case OT_DepthBias:
				desc.rasterizerDesc.depthBias = option->value.floatValue;
				isDefault = false;
				break;
			case OT_SDepthBias:
				desc.rasterizerDesc.slopeScaledDepthBias = option->value.floatValue;
				isDefault = false;
				break;
			case OT_DepthClip:
				desc.rasterizerDesc.depthClipEnable = option->value.intValue > 0;
				isDefault = false;
				break;
			case OT_Scissor:
				desc.rasterizerDesc.scissorEnable = option->value.intValue > 0;
				isDefault = false;
				break;
			case OT_Multisample:
				desc.rasterizerDesc.multisampleEnable = option->value.intValue > 0;
				isDefault = false;
				break;
			case OT_AALine:
				desc.rasterizerDesc.antialiasedLineEnable = option->value.intValue > 0;
				isDefault = false;
				break;
			default:
				break;
			}
		}

		return !isDefault;
	}

	bool BSLFXCompiler::parseDepthState(PassData& passData, ASTFXNode* depthNode)
	{
		if (depthNode == nullptr || depthNode->type != NT_Depth)
			return false;

		bool isDefault = true;

		for (int i = 0; i < depthNode->options->count; i++)
		{
			NodeOption* option = &depthNode->options->entries[i];

			switch (option->type)
			{
			case OT_DepthRead:
				passData.depthStencilDesc.depthReadEnable = option->value.intValue > 0;
				isDefault = false;
				break;
			case OT_DepthWrite:
				passData.depthStencilDesc.depthWriteEnable = option->value.intValue > 0;
				isDefault = false;
				break;
			case OT_CompareFunc:
				passData.depthStencilDesc.depthComparisonFunc = parseCompFunc((CompFuncValue)option->value.intValue);
				isDefault = false;
				break;
			default:
				break;
			}
		}

		return !isDefault;
	}

	bool BSLFXCompiler::parseStencilState(PassData& passData, ASTFXNode* stencilNode)
	{
		if (stencilNode == nullptr || stencilNode->type != NT_Stencil)
			return false;

		bool isDefault = true;

		for (int i = 0; i < stencilNode->options->count; i++)
		{
			NodeOption* option = &stencilNode->options->entries[i];

			switch (option->type)
			{
			case OT_Enabled:
				passData.depthStencilDesc.stencilEnable = option->value.intValue > 0;
				isDefault = false;
				break;
			case OT_StencilReadMask:
				passData.depthStencilDesc.stencilReadMask = (UINT8)option->value.intValue;
				isDefault = false;
				break;
			case OT_StencilWriteMask:
				passData.depthStencilDesc.stencilWriteMask = (UINT8)option->value.intValue;
				isDefault = false;
				break;
			case OT_StencilOpFront:
				parseStencilFront(passData.depthStencilDesc, option->value.nodePtr);
				isDefault = false;
				break;
			case OT_StencilOpBack:
				parseStencilBack(passData.depthStencilDesc, option->value.nodePtr);
				isDefault = false;
				break;
			case OT_StencilRef:
				passData.stencilRefValue = option->value.intValue;
				break;
			default:
				break;
			}
		}

		return !isDefault;
	}
	
	void BSLFXCompiler::parseCodeBlock(ASTFXNode* codeNode, const Vector<String>& codeBlocks, PassData& passData)
	{
		if (codeNode == nullptr || (codeNode->type != NT_Code))
		{
			return;
		}

		UINT32 index = (UINT32)-1;
		for (int j = 0; j < codeNode->options->count; j++)
		{
			if (codeNode->options->entries[j].type == OT_Index)
				index = codeNode->options->entries[j].value.intValue;
		}

		if (index != (UINT32)-1 && index < (UINT32)codeBlocks.size())
		{
			switch (codeNode->type)
			{
			case NT_Code:
				passData.code += codeBlocks[index];
				break;
			default:
				break;
			}
		}
	}

	void BSLFXCompiler::parsePass(ASTFXNode* passNode, const Vector<String>& codeBlocks, PassData& passData)
	{
		if (passNode == nullptr || passNode->type != NT_Pass)
			return;

		for (int i = 0; i < passNode->options->count; i++)
		{
			NodeOption* option = &passNode->options->entries[i];

			switch (option->type)
			{
			case OT_Blend:
				passData.blendIsDefault &= !parseBlendState(passData, option->value.nodePtr);
				break;
			case OT_Raster:
				passData.rasterizerIsDefault &= !parseRasterizerState(passData, option->value.nodePtr);
				break;
			case OT_Depth:
				passData.depthStencilIsDefault &= !parseDepthState(passData, option->value.nodePtr);
				break;
			case OT_Stencil:
				passData.depthStencilIsDefault &= !parseStencilState(passData, option->value.nodePtr);
				break;
			case OT_Code:
				parseCodeBlock(option->value.nodePtr, codeBlocks, passData);
				break;
			default:
				break;
			}
		}
	}

	void BSLFXCompiler::parseTechnique(ASTFXNode* techniqueNode, const Vector<String>& codeBlocks, TechniqueData& techniqueData)
	{
		if (techniqueNode == nullptr || (techniqueNode->type != NT_Technique && techniqueNode->type != NT_Mixin))
			return;

		// There must always be at least one pass
		if(techniqueData.passes.empty())
		{
			techniqueData.passes.push_back(PassData());
			techniqueData.passes.back().seqIdx = 0;
		}

		PassData combinedCommonPassData;

		UINT32 nextPassIdx = 0;
		// Go in reverse because options are added in reverse order during parsing
		for (int i = techniqueNode->options->count - 1; i >= 0; i--)
		{
			NodeOption* option = &techniqueNode->options->entries[i];

			switch (option->type)
			{
			case OT_Pass:
			{
				UINT32 passIdx = nextPassIdx;
				PassData* passData = nullptr;
				for (auto& entry : techniqueData.passes)
				{
					if (entry.seqIdx == passIdx)
						passData = &entry;
				}

				if (passData == nullptr)
				{
					techniqueData.passes.push_back(PassData());
					passData = &techniqueData.passes.back();

					passData->seqIdx = passIdx;
				}

				nextPassIdx = std::max(nextPassIdx, passIdx) + 1;
				passData->code = combinedCommonPassData.code + passData->code;
				
				ASTFXNode* passNode = option->value.nodePtr;
				parsePass(passNode, codeBlocks, *passData);
			}
				break;
			case OT_Code:
			{
				PassData commonPassData;
				parseCodeBlock(option->value.nodePtr, codeBlocks, commonPassData);

				for (auto& passData : techniqueData.passes)
					passData.code += commonPassData.code;

				combinedCommonPassData.code += commonPassData.code;
			}
				break;
			default:
				break;
			}
		}

		// Parse common pass states
		for (int i = 0; i < techniqueNode->options->count; i++)
		{
			NodeOption* option = &techniqueNode->options->entries[i];

			switch (option->type)
			{
			case OT_Blend:
				for (auto& passData : techniqueData.passes)
					passData.blendIsDefault &= !parseBlendState(passData, option->value.nodePtr);
				break;
			case OT_Raster:
				for (auto& passData : techniqueData.passes)
					passData.rasterizerIsDefault &= !parseRasterizerState(passData, option->value.nodePtr);
				break;
			case OT_Depth:
				for (auto& passData : techniqueData.passes)
					passData.depthStencilIsDefault &= !parseDepthState(passData, option->value.nodePtr);
				break;
			case OT_Stencil:
				for (auto& passData : techniqueData.passes)
					passData.depthStencilIsDefault &= !parseStencilState(passData, option->value.nodePtr);
				break;
			default:
				break;
			}
		}
	}

	void BSLFXCompiler::parseOptions(ASTFXNode* optionsNode, SHADER_DESC& shaderDesc)
	{
		if (optionsNode == nullptr || optionsNode->type != NT_Options)
			return;

		for (int i = optionsNode->options->count - 1; i >= 0; i--)
		{
			NodeOption* option = &optionsNode->options->entries[i];

			switch (option->type)
			{
			case OT_Separable:
				shaderDesc.separablePasses = option->value.intValue > 1;
				break;
			case OT_Sort:
				shaderDesc.queueSortType = parseSortType((CullAndSortModeValue)option->value.intValue);
				break;
			case OT_Priority:
				shaderDesc.queuePriority = option->value.intValue;
				break;
			case OT_Transparent:
				shaderDesc.flags |= (UINT32)ShaderFlags::Transparent;
				break;
			default:
				break;
			}
		}
	}

	BSLFXCompileResult BSLFXCompiler::parseShader(const String& name, ParseState* parseState, Vector<String>& codeBlocks)
	{
		BSLFXCompileResult output;

		if (parseState->rootNode == nullptr || parseState->rootNode->type != NT_Shader)
		{
			parseStateDelete(parseState);

			output.errorMessage = "Root not is null or not a shader.";
			return output;
		}

		SHADER_DESC shaderDesc;
		Vector<pair<ASTFXNode*, TechniqueData>> techniqueData;

		// Go in reverse because options are added in reverse order during parsing
		for (int i = parseState->rootNode->options->count - 1; i >= 0; i--)
		{
			NodeOption* option = &parseState->rootNode->options->entries[i];

			switch (option->type)
			{
			case OT_Options:
				parseOptions(option->value.nodePtr, shaderDesc);
				break;
			case OT_Technique:
			{
				// We initially parse only meta-data, so we can handle out-of-order technique definitions
				TechniqueMetaData metaData = parseTechniqueMetaData(option->value.nodePtr);

				techniqueData.push_back(std::make_pair(option->value.nodePtr, TechniqueData()));
				TechniqueData& data = techniqueData.back().second;
				data.metaData = metaData;

				break;
			}
			default:
				break;
			}
		}

		bool* techniqueWasParsed = bs_stack_alloc<bool>((UINT32)techniqueData.size());
		std::function<bool(const TechniqueMetaData&, TechniqueData&)> parseInherited = 
			[&](const TechniqueMetaData& metaData, TechniqueData& outTechnique)
		{
			for (auto riter = metaData.includes.rbegin(); riter != metaData.includes.rend(); ++riter)
			{
				const String& includes = *riter;

				UINT32 baseIdx = -1;
				for(UINT32 i = 0; i < (UINT32)techniqueData.size(); i++)
				{
					auto& entry = techniqueData[i];
					if (!entry.second.metaData.isMixin)
						continue;

					if (entry.second.metaData.name == includes)
					{
						bool matches = entry.second.metaData.language == metaData.language || entry.second.metaData.language == "Any";
						matches &= entry.second.metaData.renderer == metaData.renderer || entry.second.metaData.renderer == RendererAny;

						// We want the last matching technique, in order to allow techniques to override each other
						if (matches)
							baseIdx = i;
					}
				}

				if (baseIdx != -1)
				{
					auto& entry = techniqueData[baseIdx];

					// Was already parsed previously, don't parse it multiple times (happens when multiple techniques 
					// include the same mixin)
					if (techniqueWasParsed[baseIdx])
						continue;

					if (!parseInherited(entry.second.metaData, outTechnique))
						return false;

					parseTechnique(entry.first, codeBlocks, outTechnique);
					techniqueWasParsed[baseIdx] = true;
					
				}
				else
				{
					output.errorMessage = "Mixin \"" + includes + "\" cannot be found.";
					return false;
				}
			}

			return true;
		};


		// Actually parse techniques
		for (auto& entry : techniqueData)
		{
			const TechniqueMetaData& metaData = entry.second.metaData;
			if (metaData.isMixin)
				continue;

			bs_zero_out(techniqueWasParsed, techniqueData.size());
			if (!parseInherited(metaData, entry.second))
			{
				parseStateDelete(parseState);
				return output;
			}

			parseTechnique(entry.first, codeBlocks, entry.second);
		}

		bs_stack_free(techniqueWasParsed);

		// Parse extended HLSL code and generate per-program code, also convert to GLSL/VKSL
		UINT32 end = (UINT32)techniqueData.size();
		for(UINT32 i = 0; i < end; i++)
		{
			const TechniqueMetaData& metaData = techniqueData[i].second.metaData;
			if (metaData.isMixin)
				continue;

			TechniqueData& hlslTechnique = techniqueData[i].second;

			TechniqueData glslTechnique = techniqueData[i].second;
			glslTechnique.metaData.language = "glsl";

			TechniqueData vkslTechnique = techniqueData[i].second;
			vkslTechnique.metaData.language = "vksl";

			UINT32 numPasses = (UINT32)hlslTechnique.passes.size();

			for(UINT32 j = 0; j < numPasses; j++)
			{
				PassData& hlslPassData = hlslTechnique.passes[j];
				PassData& glslPassData = glslTechnique.passes[j];
				PassData& vkslPassData = vkslTechnique.passes[j];

				// Clean non-standard HLSL 
				static const std::regex regex("\\[\\s*layout\\s*\\(.*\\)\\s*\\]|\\[\\s*internal\\s*\\]|\\[\\s*color\\s*\\]");
				hlslPassData.code = regex_replace(hlslPassData.code, regex, "");

				// Find valid entry points and parameters
				// Note: XShaderCompiler needs to do a full pass when doing reflection, and for each individual program
				// type. If performance is ever important here it could be good to update XShaderCompiler so it can
				// somehow save the AST and then re-use it for multiple actions.
				Vector<GpuProgramType> types;
				reflectHLSL(glslPassData.code, shaderDesc, types);

				UINT32 glslBinding = 0;
				UINT32 vkslBinding = 0;

				// Cross-compile for all detected shader types
				// Note: I'm just copying HLSL code as-is. This code will contain all entry points which could have
				// an effect on compile time. It would be ideal to remove dead code depending on program type. This would
				// involve adding a HLSL code generator to XShaderCompiler.
				for(auto& type : types)
				{
					switch(type)
					{
					case GPT_VERTEX_PROGRAM:
						hlslPassData.vertexCode = hlslPassData.code;
						glslPassData.vertexCode = HLSLtoGLSL(glslPassData.code, GPT_VERTEX_PROGRAM, false, glslBinding);
						vkslPassData.vertexCode = HLSLtoGLSL(glslPassData.code, GPT_VERTEX_PROGRAM, true, vkslBinding);
						break;
					case GPT_FRAGMENT_PROGRAM:
						hlslPassData.fragmentCode = hlslPassData.code;
						glslPassData.fragmentCode = HLSLtoGLSL(glslPassData.code, GPT_FRAGMENT_PROGRAM, false, glslBinding);
						vkslPassData.fragmentCode = HLSLtoGLSL(glslPassData.code, GPT_FRAGMENT_PROGRAM, true, vkslBinding);
						break;
					case GPT_GEOMETRY_PROGRAM:
						hlslPassData.geometryCode = hlslPassData.code;
						glslPassData.geometryCode = HLSLtoGLSL(glslPassData.code, GPT_GEOMETRY_PROGRAM, false, glslBinding);
						vkslPassData.geometryCode = HLSLtoGLSL(glslPassData.code, GPT_GEOMETRY_PROGRAM, true, vkslBinding);
						break;
					case GPT_HULL_PROGRAM:
						hlslPassData.hullCode = hlslPassData.code;
						glslPassData.hullCode = HLSLtoGLSL(glslPassData.code, GPT_HULL_PROGRAM, false, glslBinding);
						vkslPassData.hullCode = HLSLtoGLSL(glslPassData.code, GPT_HULL_PROGRAM, true, vkslBinding);
						break;
					case GPT_DOMAIN_PROGRAM:
						hlslPassData.domainCode = hlslPassData.code;
						glslPassData.domainCode = HLSLtoGLSL(glslPassData.code, GPT_DOMAIN_PROGRAM, false, glslBinding);
						vkslPassData.domainCode = HLSLtoGLSL(glslPassData.code, GPT_DOMAIN_PROGRAM, true, vkslBinding);
						break;
					case GPT_COMPUTE_PROGRAM:
						hlslPassData.computeCode = hlslPassData.code;
						glslPassData.computeCode = HLSLtoGLSL(glslPassData.code, GPT_COMPUTE_PROGRAM, false, glslBinding);
						vkslPassData.computeCode = HLSLtoGLSL(glslPassData.code, GPT_COMPUTE_PROGRAM, true, vkslBinding);
						break;
					}
				}
			}

			techniqueData.push_back(std::make_pair(techniqueData[i].first, glslTechnique));
			techniqueData.push_back(std::make_pair(techniqueData[i].first, vkslTechnique));
		}

		Vector<SPtr<Technique>> techniques;
		for(auto& entry : techniqueData)
		{
			const TechniqueMetaData& metaData = entry.second.metaData;
			if (metaData.isMixin)
				continue;

			Map<UINT32, SPtr<Pass>, std::greater<UINT32>> passes;
			for (auto& passData : entry.second.passes)
			{
				PASS_DESC passDesc;

				if (!passData.blendIsDefault)
					passDesc.blendState = BlendState::create(passData.blendDesc);

				if (!passData.rasterizerIsDefault)
					passDesc.rasterizerState = RasterizerState::create(passData.rasterizerDesc);

				if (!passData.depthStencilIsDefault)
					passDesc.depthStencilState = DepthStencilState::create(passData.depthStencilDesc);

				GPU_PROGRAM_DESC desc;
				desc.language = metaData.language;

				bool isHLSL = desc.language == "hlsl";
				if (!passData.vertexCode.empty())
				{
					desc.entryPoint = isHLSL ? "vsmain" : "main";
					desc.source = passData.vertexCode;
					desc.type = GPT_VERTEX_PROGRAM;

					passDesc.vertexProgram = GpuProgram::create(desc);
				}

				if (!passData.fragmentCode.empty())
				{
					desc.entryPoint = isHLSL ? "fsmain" : "main";
					desc.source = passData.fragmentCode;
					desc.type = GPT_FRAGMENT_PROGRAM;

					passDesc.fragmentProgram = GpuProgram::create(desc);
				}

				if (!passData.geometryCode.empty())
				{
					desc.entryPoint = isHLSL ? "gsmain" : "main";
					desc.source = passData.geometryCode;
					desc.type = GPT_GEOMETRY_PROGRAM;

					passDesc.geometryProgram = GpuProgram::create(desc);
				}

				if (!passData.hullCode.empty())
				{
					desc.entryPoint = isHLSL ? "hsmain" : "main";
					desc.source = passData.hullCode;
					desc.type = GPT_HULL_PROGRAM;

					passDesc.hullProgram = GpuProgram::create(desc);
				}

				if (!passData.domainCode.empty())
				{
					desc.entryPoint = isHLSL ? "dsmain" : "main";
					desc.source = passData.domainCode;
					desc.type = GPT_DOMAIN_PROGRAM;

					passDesc.domainProgram = GpuProgram::create(desc);
				}

				if (!passData.computeCode.empty())
				{
					desc.entryPoint = isHLSL ? "csmain" : "main";
					desc.source = passData.computeCode;
					desc.type = GPT_COMPUTE_PROGRAM;

					passDesc.computeProgram = GpuProgram::create(desc);
				}

				passDesc.stencilRefValue = passData.stencilRefValue;

				SPtr<Pass> pass = Pass::create(passDesc);
				if (pass != nullptr)
					passes[passData.seqIdx] = pass;
			}

			Vector<SPtr<Pass>> orderedPasses;
			for (auto& KVP : passes)
				orderedPasses.push_back(KVP.second);

			if (orderedPasses.size() > 0)
			{
				SPtr<Technique> technique = Technique::create(metaData.language, metaData.renderer, metaData.tags, 
					orderedPasses);
				techniques.push_back(technique);
			}
		}

		Vector<String> includes;
		IncludeLink* includeLink = parseState->includes;
		while(includeLink != nullptr)
		{
			String includeFilename = includeLink->data->filename;

			auto iterFind = std::find(includes.begin(), includes.end(), includeFilename);
			if (iterFind == includes.end())
				includes.push_back(includeFilename);

			includeLink = includeLink->next;
		}

		parseStateDelete(parseState);

		output.shader = Shader::_createPtr(name, shaderDesc, techniques);
		output.shader->setIncludeFiles(includes);

		return output;
	}

	String BSLFXCompiler::removeQuotes(const char* input)
	{
		UINT32 len = (UINT32)strlen(input);
		String output(len - 2, ' ');

		for (UINT32 i = 0; i < (len - 2); i++)
			output[i] = input[i + 1];

		return output;
	}
}