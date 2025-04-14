//Unreal® Engine, Copyright 1998 – 2023, Epic Games, Inc. All rights reserved.

#include "PXR_DPHMD.h"
#include "CoreMinimal.h"
#include "ClearQuad.h"								// 清理四元数
#include "DefaultSpectatorScreenController.h"		// 默认观看者屏幕控制器
#include "ScreenRendering.h"
#include "RHIStaticStates.h"
#include "Developer//Settings//Public/ISettingsModule.h"
#include "PXR_Log.h"


// 如果是windows平台
#if PLATFORM_WINDOWS
#include "DynamicRHI.h"
#include "D3D11RHI.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#endif

#include "Slate/SceneViewport.h"
#include "Engine/GameEngine.h"
#include "GameFramework/PlayerController.h"
#include "IHeadMountedDisplayVulkanExtensions.h"
#include "HeadMountedDisplayFunctionLibrary.h"

#include "CanvasItem.h"
#include "CommonRenderResources.h"

#include "PXR_DPSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "PostProcess/DrawRectangle.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#include "ISettingsModule.h"
#endif

// 定义数组大小
#ifndef ARRAYSIZE
#define ARRAYSIZE( a ) ( sizeof( ( a ) ) / sizeof( ( a )[ 0 ] ) )
#endif

// 预览的FOV 系统默认的是90度FOV(这里101)
static constexpr float PreviewFov = 101.0f;

// 定义日志类型
DEFINE_LOG_CATEGORY(LogPICODP);
/** Helper function for acquiring the appropriate FSceneViewport */
// NOTE: 局部函数，用于查找视口
FSceneViewport* FindSceneViewport()
{
	// NOTE: 如果不是编辑器
	if (!GIsEditor)
	{
		// 获取游戏引擎
		UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
		// 返回游戏引擎的场景视口
		return GameEngine->SceneViewport.Get();
	}
	// NOTE: 编辑器情况下
#if WITH_EDITOR
	else
	{
		// 编辑器引擎
		UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
		// PIE视口
		FSceneViewport* PIEViewport = (FSceneViewport*)EditorEngine->GetPIEViewport();
		// 如果不为空并且是以立体渲染启动的
		if (PIEViewport != nullptr && PIEViewport->IsStereoRenderingAllowed())
		{
			// PIE is setup for stereo rendering
			// 让PIE以立体模式渲染
			return PIEViewport;
		}
		else
		{
			// Check to see if the active editor viewport is drawing in stereo mode
			// @todo vreditor: Should work with even non-active viewport!
			// 检查激活的编辑器视口是在绘制立体模式
			// 获取编辑器视口
			FSceneViewport* EditorViewport = (FSceneViewport*)EditorEngine->GetActiveViewport();
			// 编辑器视口
			if (EditorViewport != nullptr && EditorViewport->IsStereoRenderingAllowed())
			{
				// 让编辑器以立体视口运行
				return EditorViewport;
			}
		}
	}
#endif
	// 不然直接返回空
	return nullptr;
}

//---------------------------------------------------
// PICODP Plugin Implementation
//---------------------------------------------------

// PICO插件
class FPICODPPlugin : public IPICOXRDPModule
{
	/** IHeadMountedDisplayModule implementation */
	// 创建追踪系统
	virtual TSharedPtr<class IXRTrackingSystem, ESPMode::ThreadSafe> CreateTrackingSystem() override;

	// 获取模块键值名
	virtual FString GetModuleKeyName() const override
	{
		return FString(TEXT("PICOXRPreview"));
	}
	
	// libusb_DLL句柄
	void* LibusbDllHandle;
	// ps_base_DLL句柄
	void* ps_baseDllHandle;
	// ps_common_Dll句柄
	void* ps_commonDllHandle;
	// ps_设备_运行时
	void* ps_driver_runtime;
	// 载入PICODP模块
	bool LoadPICODPModule();
	// 卸载PICODP模块
	void UnloadPICODPModule();

public:
	FPICODPPlugin()
	{
	}

	// 启动模块
	virtual void StartupModule() override
	{
		IHeadMountedDisplayModule::StartupModule();
		LoadPICODPModule();
	}

	// 结束模块
	virtual void ShutdownModule() override
	{
		IHeadMountedDisplayModule::ShutdownModule();
		UnloadPICODPModule();
	}

	// 初始化：只是输出日志
	bool Initialize()
	{
		PXR_LOGD(PxrUnreal, "PXR_LivePreview Initialize!");
		return true;
	}

	// 每次初始化
	virtual bool PreInit() override
	{
		float ModulePriority;
		// 如果没有读到配置
		if (!GConfig->GetFloat(TEXT("HMDPluginPriority"), *GetModuleKeyName(), ModulePriority, GEngineIni))
		{
			// 设置最高优先级
			ModulePriority = 999.0f;
			GConfig->SetFloat(TEXT("HMDPluginPriority"), *GetModuleKeyName(), ModulePriority, GEngineIni);
		}
		
		return true;
	}

	// 头戴显示器是否链接
	virtual bool IsHMDConnected() override
	{
		return true;
	}

#if PLATFORM_WINDOWS
	// D3DApi级别
	enum class D3DApiLevel
	{
		Undefined,
		Direct3D11,
		Direct3D12
	};
	// 获取D3D——API级别
	static inline D3DApiLevel GetD3DApiLevel()
	{
		// RHI字符串
		FString RHIString;
		
		{
			// 硬件详情
			FString HardwareDetails = FHardwareInfo::GetHardwareDetailsString();
			// RHI查找
			FString RHILookup = NAME_RHI.ToString() + TEXT("=");
			// 当目前这个日志屁用没有
			FParse::Value(*HardwareDetails, *RHILookup, RHIString);
			PXR_LOGD(PxrUnreal, "PXR_LivePreview RHIString:%s", *RHIString);
			// 提取字符串到输出中
			if (!FParse::Value(*HardwareDetails, *RHILookup, RHIString))
			{
				// RHI might not be up yet. Let's check the command-line and see if DX12 was specified.
				// This will get hit on startup since we don't have RHI details during stereo device bringup. 
				// This is not a final fix; we should probably move the stereo device init to later on in startup.
				// NOTE: 是否强制DX12
				bool bForceD3D12 = FParse::Param(FCommandLine::Get(), TEXT("d3d12")) || FParse::Param(FCommandLine::Get(), TEXT("dx12"));
				return bForceD3D12 ? D3DApiLevel::Direct3D12 : D3DApiLevel::Direct3D11;
			}
		}
		// 校验D3D的应用级别
		if (RHIString == TEXT("D3D11"))
		{
			return D3DApiLevel::Direct3D11;
		}
		if (RHIString == TEXT("D3D12"))
		{
			return D3DApiLevel::Direct3D12;
		}

		return D3DApiLevel::Undefined;
	}

#endif

private:
	TSharedPtr<IHeadMountedDisplayVulkanExtensions, ESPMode::ThreadSafe> VulkanExtensions;
};

// 卸载模块
void FPICODPPlugin::UnloadPICODPModule()
{
	// 释放各个DLL       
	if (LibusbDllHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Freeing LibusbDllHandle."));
		FPlatformProcess::FreeDllHandle(LibusbDllHandle);
		LibusbDllHandle = nullptr;
	}
	if (ps_baseDllHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Freeing ps_baseDllHandle."));
		FPlatformProcess::FreeDllHandle(ps_baseDllHandle);
		ps_baseDllHandle = nullptr;
	}
	if (ps_commonDllHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Freeing ps_commonDllHandle."));
		FPlatformProcess::FreeDllHandle(ps_commonDllHandle);
		ps_commonDllHandle = nullptr;
	}
	if (ps_driver_runtime)
	{
		UE_LOG(LogHMD, Log, TEXT("Freeing ps_driver_runtime."));
		FPlatformProcess::FreeDllHandle(ps_driver_runtime);
		ps_driver_runtime = nullptr;
	}
}

bool FPICODPPlugin::LoadPICODPModule()
{
#if PLATFORM_WINDOWS
#if PLATFORM_64BITS
	// 动态DLL的目录
	FString StreamerDLLDir = FPaths::ProjectPluginsDir() / FString::Printf(TEXT("PICOLivePreview/Source/ThirdParty/bin/"));
	// 推送Dll目录
	FPlatformProcess::PushDllDirectory(*StreamerDLLDir);
	LibusbDllHandle = FPlatformProcess::GetDllHandle(*(StreamerDLLDir + "libusb-1.0.dll"));
	ps_baseDllHandle = FPlatformProcess::GetDllHandle(*(StreamerDLLDir + "ps_base.dll"));
	ps_commonDllHandle = FPlatformProcess::GetDllHandle(*(StreamerDLLDir + "ps_common.dll"));
	ps_driver_runtime = FPlatformProcess::GetDllHandle(*(StreamerDLLDir + "ps_driver_runtime.dll"));
	// 弹出DLL目录
	FPlatformProcess::PopDllDirectory(*StreamerDLLDir);

	// 校验各个dLL是否载入了
	if (!LibusbDllHandle
		|| !ps_baseDllHandle
		|| !ps_commonDllHandle
		|| !ps_driver_runtime)
	{
		UE_LOG(LogHMD, Log, TEXT("Failed to load PICODP library."));
		return false;
	}

#endif
#endif
	return true;
}

IMPLEMENT_MODULE(FPICODPPlugin, PICOXRDPHMD)

// 创建追踪系统
TSharedPtr<class IXRTrackingSystem, ESPMode::ThreadSafe> FPICODPPlugin::CreateTrackingSystem()
{
	// 日志
	PXR_LOGD(PxrUnreal, "PXR_LivePreview Start CreateTrackingSystem!");
	// 获取D3D Api级别
	auto level = FPICODPPlugin::GetD3DApiLevel();

	// 这里是为了确保是用DX11来执行
	if (level == FPICODPPlugin::D3DApiLevel::Direct3D11)
	{
		TSharedPtr<FPICOXRHMDDP, ESPMode::ThreadSafe> PICODPHMD = FSceneViewExtensions::NewExtension<FPICOXRHMDDP>(this);
		if (PICODPHMD)
		{
			return PICODPHMD;
		}
	}
	return nullptr;
}


//---------------------------------------------------
// PICODP IHeadMountedDisplay Implementation
//---------------------------------------------------

#if STEAMVR_SUPPORTED_PLATFORMS


bool FPICOXRHMDDP::IsHMDConnected()
{
	return true;
}

bool FPICOXRHMDDP::IsHMDEnabled() const
{
	return bHmdEnabled;
}

EHMDWornState::Type FPICOXRHMDDP::GetHMDWornState()
{
	return HmdWornState;
}

// 启用头衔
void FPICOXRHMDDP::EnableHMD(bool enable)
{
	bHmdEnabled = enable;
	// 如果没有启用就不启用立体
	if (!bHmdEnabled)
	{
		EnableStereo(false);
	}
}

// NOTE: 获取信息(这里为啥给空的啊？)
bool FPICOXRHMDDP::GetHMDMonitorInfo(MonitorInfo& MonitorDesc)
{
	MonitorDesc.MonitorName = "";
	MonitorDesc.MonitorId = 0;
	MonitorDesc.DesktopX = MonitorDesc.DesktopY = MonitorDesc.ResolutionX = MonitorDesc.ResolutionY = 0;
	return false;
}

// NOTE： 这里为啥也给空（这个也是敷衍，也不知道公开了没）
void FPICOXRHMDDP::GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees) const
{
	OutHFOVInDegrees = 0.0f;
	OutVFOVInDegrees = 0.0f;
}

// NOTE: 是否支持位置追踪 (好敷衍，不过可以看看公开了接口没)
bool FPICOXRHMDDP::DoesSupportPositionalTracking() const
{
	return true;
}

// NOTE: 是否为有效的追踪位置 (好敷衍，不过可以看看公开了接口没) 
bool FPICOXRHMDDP::HasValidTrackingPosition()
{
	return true;
}

// NOTE： 也是敷衍，就是给出了一套空的信息
bool FPICOXRHMDDP::GetTrackingSensorProperties(int32 SensorId, FQuat& OutOrientation, FVector& OutOrigin, FXRSensorProperties& OutSensorProperties)
{
	OutOrigin = FVector::ZeroVector;
	OutOrientation = FQuat::Identity;
	OutSensorProperties = FXRSensorProperties();
	return true;
}

// 序列编号也返回空
FString FPICOXRHMDDP::GetTrackedDevicePropertySerialNumber(int32 DeviceId)
{
	return FString();
}

// 设置间隔距离也为空
void FPICOXRHMDDP::SetInterpupillaryDistance(float NewInterpupillaryDistance)
{
}

// 获取间隔距离
float FPICOXRHMDDP::GetInterpupillaryDistance() const
{
	return 0.064f;
}

// 获取当前位置姿态
bool FPICOXRHMDDP::GetCurrentPose(int32 DeviceId, FQuat& CurrentOrientation, FVector& CurrentPosition)
{
	FQuat tempCurrentOrientation = FQuat::Identity;

	// 是否在传输中
	if (FPICOXRDPManager::IsStreaming())
	{
		FPICOXRDPManager::GetHMDPositionAndRotation(CurrentPosition, tempCurrentOrientation);
		CurrentPosition = FVector(-CurrentPosition.Z, CurrentPosition.X, CurrentPosition.Y) * 100;
		CurrentOrientation.X = tempCurrentOrientation.Z;
		CurrentOrientation.Y = -tempCurrentOrientation.X;
		CurrentOrientation.Z = -tempCurrentOrientation.Y;
		CurrentOrientation.W = tempCurrentOrientation.W;
		//Todo:Prevent crashes caused by invalid Unnormalized data
		CurrentOrientation.Normalize();

		return true;
	}
	return false;
}

void FPICOXRHMDDP::SetTrackingOrigin(EHMDTrackingOrigin::Type NewOrigin)
{
}

EHMDTrackingOrigin::Type FPICOXRHMDDP::GetTrackingOrigin() const
{
	return EHMDTrackingOrigin::LocalFloor;
}

bool FPICOXRHMDDP::GetFloorToEyeTrackingTransform(FTransform& OutStandingToSeatedTransform) const
{
	bool bSuccess = false;
	return bSuccess;
}

// 忽略
FVector2D FPICOXRHMDDP::GetPlayAreaBounds(EHMDTrackingOrigin::Type Origin) const
{
	FVector2D Bounds;
	if (Origin == EHMDTrackingOrigin::Stage)
	{
		return Bounds;
	}

	return FVector2D::ZeroVector;
}

void FPICOXRHMDDP::RecordAnalytics()
{
}

// 缩放比例
float FPICOXRHMDDP::GetWorldToMetersScale() const
{
	return 100.0f;
}


// 枚举追踪涉笔
bool FPICOXRHMDDP::EnumerateTrackedDevices(TArray<int32>& TrackedIds, EXRTrackedDeviceType DeviceType)
{
	TrackedIds.Empty();
	// 任意的或者头戴设备
	if (DeviceType == EXRTrackedDeviceType::Any || DeviceType == EXRTrackedDeviceType::HeadMountedDisplay)
	{
		TrackedIds.Add(IXRTrackingSystem::HMDDeviceId);
		return true;
	}
	return false;
}

// 追踪
bool FPICOXRHMDDP::IsTracking(int32 DeviceId)
{
	return true;
}

// 不纠正颜色
bool FPICOXRHMDDP::IsChromaAbCorrectionEnabled() const
{
	return false;
}

// 开始时
void FPICOXRHMDDP::OnBeginPlay(FWorldContext& InWorldContext)
{
#if WITH_EDITOR
	if (!InitializedSucceeded)
	{
		return;
	}

	if (GIsEditor)
	{
		UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine);
		// 编辑器引擎
		if (EdEngine->GetPlayInEditorSessionInfo().IsSet())
		{
			// 是否可以VR预览  
			bIsVRPreview = EdEngine->GetPlayInEditorSessionInfo()->OriginalRequestParams.SessionPreviewTypeOverride ==
				EPlaySessionPreviewType::VRPreview;
		}
	}

	// 是否VR预览
	if (bIsVRPreview)
	{
		// 设置观察者模式为单眼
		UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::SingleEyeCroppedToFill);
		// SingleEyeLetterboxed 带有黑边的
		// 单眼也是很不错的。
		// 修正帧率
		GEngine->bUseFixedFrameRate = true;
		GEngine->FixedFrameRate = 72;

		// 链接成功日志
		if (FPICOXRDPManager::IsConnectToServiceSucceed()
			&& FPICOXRDPManager::OnBeginPlayStartStreaming())
		{
			PXR_LOGD(PxrUnreal, "PXR_LivePreview  BeginPlay Succeed!");
		}
		else
		{
			PXR_LOGD(PxrUnreal, "PXR_LivePreview  StartStreaming Failed!Please check if PDC is Launching and restart vr preview again");
		}
	}

#endif
}

// 结束时
void FPICOXRHMDDP::OnEndPlay(FWorldContext& InWorldContext)
{
	// 禁用立体
	if (!GEnableVREditorHacks)
	{
		EnableStereo(false);
	}

	// 如果是VR预览
	if (bIsVRPreview)
	{
		// 恢复默认设置
		GEngine->bUseFixedFrameRate = false;
		GEngine->FixedFrameRate = 30;
		if (FPICOXRDPManager::SetHandTrackingEnable(false))
		{
			PXR_LOGD(PxrUnreal, "PXR_LivePreview disable HandTracking Succeed!");
		}
		// 停止流
		FPICOXRDPManager::OnEndPlayStopStreaming();
	}
}

const FName FPICOXRHMDDP::SystemName(TEXT("PICOXRPreview"));

// 版本空
FString FPICOXRHMDDP::GetVersionString() const
{
	return FString();
}

// 开始游戏帧的是偶
bool FPICOXRHMDDP::OnStartGameFrame(FWorldContext& WorldContext)
{
	// 修正预期
	if (bStereoEnabled != bStereoDesired)
	{
		bStereoEnabled = EnableStereo(bStereoDesired);
	}
	return true;
}

// 重置位置和旋转
void FPICOXRHMDDP::ResetOrientationAndPosition(float yaw)
{
	ResetOrientation(yaw);
	ResetPosition();
}

void FPICOXRHMDDP::ResetOrientation(float Yaw)
{
	BaseOrientation = FQuat::Identity;
}

void FPICOXRHMDDP::ResetPosition()
{
	BaseOffset = FVector();
}

// 期待的旋转
void FPICOXRHMDDP::SetBaseRotation(const FRotator& BaseRot)
{
	BaseOrientation = BaseRot.Quaternion();
}

FRotator FPICOXRHMDDP::GetBaseRotation() const
{
	return FRotator::ZeroRotator;
}

void FPICOXRHMDDP::SetBaseOrientation(const FQuat& BaseOrient)
{
	BaseOrientation = BaseOrient;
}

FQuat FPICOXRHMDDP::GetBaseOrientation() const
{
	return BaseOrientation;
}

void FPICOXRHMDDP::SetBasePosition(const FVector& BasePosition)
{
	BaseOffset = BasePosition;
}

FVector FPICOXRHMDDP::GetBasePosition() const
{
	return BaseOffset;
}

// 立体是否启动
bool FPICOXRHMDDP::IsStereoEnabled() const
{
	return true;
}

// 启用立体
bool FPICOXRHMDDP::EnableStereo(bool bStereo)
{
	if (bStereoEnabled == bStereo)
	{
		return false;
	}

	if ((!bStereo))
	{
		return false;
	}

	bStereoDesired = (IsHMDEnabled()) ? bStereo : false;

	// Set the viewport to match that of the HMD display
	// 查找视口
	FSceneViewport* SceneVP = FindSceneViewport();
	if (SceneVP)
	{
		// 窗口
		TSharedPtr<SWindow> Window = SceneVP->FindWindow();
		if (Window.IsValid() && SceneVP->GetViewportWidget().IsValid())
		{
			// Set MirrorWindow state on the Window
			// 在“窗口”中设置“镜像窗口状态”
			Window->SetMirrorWindow(bStereo);

			if (bStereo)
			{
				uint32 Width, Height;
				// 宽高
				Width = WindowMirrorBoundsWidth;
				Height = WindowMirrorBoundsHeight;

				bStereoEnabled = bStereoDesired;
				// 设置宽高
				SceneVP->SetViewportSize(Width, Height);
			}
			else
			{
				//flush all commands that might call GetStereoProjectionMatrix or other functions that rely on bStereoEnabled
				// 刷新渲染命令
				FlushRenderingCommands();

				// Note: Setting before resize to ensure we don't try to allocate a new vr rt.
				bStereoEnabled = bStereoDesired;

				// 视口RHI
				FRHIViewport* const ViewportRHI = SceneVP->GetViewportRHI();
				if (ViewportRHI != nullptr)
				{
					ViewportRHI->SetCustomPresent(nullptr);
				}
				// 查找窗口
				FVector2D size = SceneVP->FindWindow()->GetSizeInScreen();
				// 设置可视尺寸
				SceneVP->SetViewportSize(size.X, size.Y);
				Window->SetViewportSizeDrivenByWindow(true);
			}
		}
	}

	// Uncap fps to enable FPS higher than 62
	GEngine->bForceDisableFrameRateSmoothing = bStereoEnabled;

	return bStereoEnabled;
}

void FPICOXRHMDDP::AdjustViewRect(int32 ViewIndex, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	SizeX = FMath::CeilToInt(IdealRenderTargetSize.X * PixelDensity);
	SizeY = FMath::CeilToInt(IdealRenderTargetSize.Y * PixelDensity);

	SizeX = SizeX / 2;
	if (ViewIndex == 0)
	{
		X += SizeX;
	}
}

// 获取相对眼睛位置
bool FPICOXRHMDDP::GetRelativeEyePose(int32 DeviceId, int32 ViewIndex, FQuat& OutOrientation, FVector& OutPosition)
{
	OutOrientation = FQuat::Identity;
	OutPosition = FVector::ZeroVector;
	if (DeviceId == IXRTrackingSystem::HMDDeviceId && (ViewIndex == 0 || ViewIndex == 1))
	{
		// 位置是0.5倍的
		OutPosition = FVector(0, (ViewIndex == 0 ? -.5 : .5) * 0.064f * GetWorldToMetersScale(), 0);
		return true;
	}
	else
	{
		return false;
	}
}

// 计算立体视口偏移
void FPICOXRHMDDP::CalculateStereoViewOffset(const int32 ViewIndex, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation)
{
	// Needed to transform world locked stereo layers
	PlayerLocation = ViewLocation;

	// Forward to the base implementation (that in turn will call the DefaultXRCamera implementation)
	FHeadMountedDisplayBase::CalculateStereoViewOffset(ViewIndex, ViewRotation, WorldToMeters, ViewLocation);
}

// 获取立体投影矩阵
FMatrix FPICOXRHMDDP::GetStereoProjectionMatrix(const int32 ViewIndex) const
{
	// 校验立体启用/ 头部追踪
	check(IsStereoEnabled() || IsHeadTrackingEnforced());
	// 投影中心偏移
	const float ProjectionCenterOffset = 0; // 0.151976421f;
	// 通道投影偏移
	const float PassProjectionOffset = (ViewIndex == 0) ? ProjectionCenterOffset : -ProjectionCenterOffset;
	// correct far and near planes for reversed-Z projection matrix
	// 世界单位缩放
	const float WorldScale = GetWorldToMetersScale() * (1.0 / 100.0f); // physical scale is 100 UUs/meter
	// 近点
	float ZNear = GNearClippingPlane * WorldScale;

	// 多个FOV
	const float HalfUpFov = FPlatformMath::Tan(BothFrustum.FovUp);
	const float HalfDownFov = FPlatformMath::Tan(BothFrustum.FovDown);
	const float HalfLeftFov = FPlatformMath::Tan(BothFrustum.FovLeft);
	const float HalfRightFov = FPlatformMath::Tan(BothFrustum.FovRight);
	// 左右的FOV
	float SumRL = (HalfRightFov + HalfLeftFov);
	// 上下的FOV
	float SumTB = (HalfUpFov + HalfDownFov);
	float InvRL = (1.0f / (HalfRightFov - HalfLeftFov));
	float InvTB = (1.0f / (HalfUpFov - HalfDownFov));
	FMatrix ProjectionMatrix = FMatrix(
		FPlane((2.0f * InvRL), 0.0f, 0.0f, 0.0f),
		FPlane(0.0f, (2.0f * InvTB), 0.0f, 0.0f),
		FPlane((SumRL * -InvRL), (SumTB * -InvTB), 0.0f, 1.0f),
		FPlane(0.0f, 0.0f, ZNear, 0.0f)) * FTranslationMatrix(FVector(PassProjectionOffset, 0, 0));
	return ProjectionMatrix;
}

// 获取失真启用
bool FPICOXRHMDDP::GetHMDDistortionEnabled(EShadingPath /* ShadingPath */) const
{
	return false;
}

// 开始渲染
void FPICOXRHMDDP::OnBeginRendering_GameThread()
{
	check(IsInGameThread());
	SpectatorScreenController->BeginRenderViewFamily();
}

// 开始渲染
void FPICOXRHMDDP::OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());
	//UpdatePoses();

	check(pBridge);
	// 桥开始渲染
	pBridge->BeginRendering_RenderThread(RHICmdList);

	check(SpectatorScreenController);
	// 观察者屏幕控制器
	SpectatorScreenController->UpdateSpectatorScreenMode_RenderThread();

	// Update PlayerOrientation used by StereoLayers positioning
	const FSceneView* MainView = ViewFamily.Views[0];
	const FQuat ViewOrientation = MainView->ViewRotation.Quaternion();
	PlayerOrientation = ViewOrientation * MainView->BaseHmdOrientation.Inverse();
}

// 获取激活渲染的桥
FXRRenderBridge* FPICOXRHMDDP::GetActiveRenderBridge_GameThread(bool /* bUseSeparateRenderTarget */)
{
	check(IsInGameThread());

	return pBridge;
}

// 计算渲染目标的尺寸
void FPICOXRHMDDP::CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	if (!IsStereoEnabled())
	{
		return;
	}

	InOutSizeX = FMath::CeilToInt(IdealRenderTargetSize.X * PixelDensity);
	InOutSizeY = FMath::CeilToInt(IdealRenderTargetSize.Y * PixelDensity);

	check(InOutSizeX != 0 && InOutSizeY != 0);
}

// 需要重新分配视口渲染目标
bool FPICOXRHMDDP::NeedReAllocateViewportRenderTarget(const FViewport& Viewport)
{
	check(IsInGameThread());

	if (IsStereoEnabled())
	{
		const uint32 InSizeX = Viewport.GetSizeXY().X;
		const uint32 InSizeY = Viewport.GetSizeXY().Y;
		const FIntPoint RenderTargetSize = Viewport.GetRenderTargetTextureSizeXY();
		uint32 NewSizeX = InSizeX, NewSizeY = InSizeY;
		CalculateRenderTargetSize(Viewport, NewSizeX, NewSizeY);
		if (NewSizeX != RenderTargetSize.X || NewSizeY != RenderTargetSize.Y)
		{
			return true;
		}
	}
	return false;
}

// DP交换链
static const uint32 PICODPSwapChainLength = 1;

// 分配渲染目标纹理
bool FPICOXRHMDDP::AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips,
	ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture,
	FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples /*= 1*/)
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview AllocatedRT!");
	if (!IsStereoEnabled())
	{
		return false;
	}

	// 交换链纹理
	TArray<FTextureRHIRef> SwapChainTextures;
	// 绑定的纹理
	FTextureRHIRef BindingTexture;

	if (pBridge != nullptr && pBridge->GetSwapChain() != nullptr && pBridge->GetSwapChain()->GetTexture2D() != nullptr
		&& pBridge->GetSwapChain()->GetTexture2D()->GetSizeX() == SizeX && pBridge->GetSwapChain()->GetTexture2D()->GetSizeY() == SizeY)
	{
		// 输出纹理的类型
		OutTargetableTexture = (FTexture2DRHIRef&)pBridge->GetSwapChain()->GetTextureRef();
		OutShaderResourceTexture = OutTargetableTexture;
		return true;
	}
	// RHI 纹理描述
	FRHITextureCreateDesc Desc =
		// 创建
		FRHITextureCreateDesc::Create2D(TEXT("FDirectPreviewHMD"))
		.SetExtent(SizeX, SizeY)		// 尺寸
		.SetFormat(PF_R8G8B8A8)			// 颜色格式
		.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource)	// 渲染目标，共享纹理
		.SetInitialState(ERHIAccess::SRVMask);		// 初始化SRV

	// 遍历交换链条（2次）
	for (uint32 SwapChainIter = 0; SwapChainIter < PICODPSwapChainLength; ++SwapChainIter)
	{
		// 目标纹理
		FTexture2DRHIRef TargetableTexture;
		// 创建纹理
		TargetableTexture = RHICreateTexture(Desc);
		// 向交换链条中添加
		SwapChainTextures.Add((FTextureRHIRef&)TargetableTexture);

		if (BindingTexture == nullptr)
		{
			// 纹理绑定
			BindingTexture = GDynamicRHI->RHICreateAliasedTexture((FTextureRHIRef&)TargetableTexture);
		}
	}
	// 获取左右纹理
	TArray<FTextureRHIRef> LeftRHITextureSwapChain = FPICOXRDPManager::GetLeftRHITextureSwapChain();
	TArray<FTextureRHIRef> RightRHITextureSwapChain = FPICOXRDPManager::GetRightRHITextureSwapChain();

	//  创建左右交换链，然后将纹理移入
	pBridge->CreateLeftSwapChain(FPICOXRDPManager::GetLeftBindingTexture(), MoveTemp(LeftRHITextureSwapChain));
	pBridge->CreateRightSwapChain(FPICOXRDPManager::GetRightBindingTexture(), MoveTemp(RightRHITextureSwapChain));

	// 创建交换链
	pBridge->CreateSwapChain(BindingTexture, MoveTemp(SwapChainTextures));
	// These are the same.
	// 这里是相同的
	OutTargetableTexture = (FTexture2DRHIRef&)BindingTexture;
	OutShaderResourceTexture = (FTexture2DRHIRef&)BindingTexture;

	return true;
}

// 构造函数（自动注册，插件指针）
FPICOXRHMDDP::FPICOXRHMDDP(const FAutoRegister& AutoRegister, IPICOXRDPModule* InPICODPPlugin) :
	FHeadMountedDisplayBase(nullptr),		// 头戴显示器
	FHMDSceneViewExtension(AutoRegister),				// 场景视图拓展
	bHmdEnabled(true),		// 启用
	HmdWornState(EHMDWornState::Unknown),		// 穿戴状态未知
	bStereoDesired(false),					// 渴望和不启用立体
	bStereoEnabled(false),
	bOcclusionMeshesBuilt(false),			// 遮罩构建
	WindowMirrorBoundsWidth(2160),			// 窗口边界宽，高1200 
	WindowMirrorBoundsHeight(2160),
	PixelDensity(1.0f),						// 像素密度：1
	HMDWornMovementThreshold(50.0f),		// 移动阈值50f
	HMDStartLocation(FVector::ZeroVector),	// 起始位置0
	BaseOrientation(FQuat::Identity),		// 旋转0
	BaseOffset(FVector::ZeroVector),		// 基础偏移0
	bIsQuitting(false),						// 不是赈灾退出
	QuitTimestamp(),						// 退出时间戳
	bShouldCheckHMDPosition(false),			// 不校验位置
	RendererModule(nullptr),				// 渲染模块空
	PICODPPlugin(InPICODPPlugin)
{
	Startup();
}

FPICOXRHMDDP::~FPICOXRHMDDP()
{
	Shutdown();
}

bool FPICOXRHMDDP::IsInitialized() const
{
	return true;
}



#define LOCTEXT_NAMESPACE "FPICOXRHMDModule"


// 注册设置
void FPICOXRHMDDP::RegisterSettings()
{
#if WITH_EDITOR
	// 设置模块
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		// 注册设置
		SettingsModule->RegisterSettings("Project", "Engine", "PICOXRLivePreview Settings",
		                                 LOCTEXT("PICOXRLivePreviewSettingsName", "PICOXR LivePreview Settings"),
		                                 LOCTEXT("PICOXRLivePreviewSettingsDescription", "Configure the PICOXRLivePreview plugin"),
		                                 GetMutableDefault<UPICOXRDPSettings>()
		);
		// 编辑器参数
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	}
#endif
}

// 注销设置
void FPICOXRHMDDP::UnregisterSettings()
{
#if WITH_EDITOR
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Engine", "PICOXRLivePreview Settings");
	}
#endif
}
#undef LOCTEXT_NAMESPACE

// NOTE: 启动
bool FPICOXRHMDDP::Startup()
{
	// 注册设置
	RegisterSettings();
	// grab a pointer to the renderer module for displaying our mirror window
	// 渲染模块名
	static const FName RendererModuleName("Renderer");
	// 渲染模块
	RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
	PXR_LOGD(PxrUnreal, "PXR_LivePreview startup begin");
	// Re-initialize the plugin if we're canceling the shutdown
	// HMD设置 
	HMDSettings = GetMutableDefault<UPICOXRDPSettings>();
	switch (HMDSettings->GraphicQuality)
	{
		// 高的质量的话
	case EGraphicQuality::EPIC:
		{
			IdealRenderTargetSize = FIntPoint(4230, 2160);
		}
	case EGraphicQuality::High:
		{
			IdealRenderTargetSize = FIntPoint(3840, 1920);
		}
		break;
		// 中等质量(75%)
	case EGraphicQuality::Medium:
		{
			IdealRenderTargetSize = FIntPoint(2880, 1440);
		}
		break;
		// 低质量(50%)
	case EGraphicQuality::Low:
		{
			// 这个地方写错了，虽然是拷贝但是，是平铺的。所以不对
			IdealRenderTargetSize = FIntPoint(2400, 1200);
		}
		break;
	default:
		;
	}


	static IConsoleVariable* PixelDensityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("vr.PixelDensity"));
	// 如果存在的话当然是按照配置值吧
	if (PixelDensityCVar)
	{
		PixelDensity = PixelDensityCVar->GetFloat();
	}else
	{
		PixelDensity = 1;
	}
	// enforce finishcurrentframe
	// 强制结束当前帧
	static IConsoleVariable* CFCFVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.finishcurrentframe"));
	CFCFVar->Set(false);

	// 创建桥
	{
		pBridge = new D3D11Bridge(this);
		ensure(pBridge != nullptr);
	}

	// 创建观看者屏幕控制器
	CreateSpectatorScreenController();

	// 初始化预览
	if (!FPICOXRDPManager::InitializeLivePreview())
	{
		PXR_LOGD(PxrUnreal, "PXR_LivePreview Initialized Failed!");
		return false;
	}

	// 初始化成功了
	InitializedSucceeded = true;

	// 链接流服务
	if (!FPICOXRDPManager::ConnectStreamingServer())
	{
		PXR_LOGD(PxrUnreal, "PXR_LivePreview ConnectServer Failed!");
		return false;
	}
	// D3D11设备
	D3D11Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
	HMDSettings = GetMutableDefault<UPICOXRDPSettings>();
	switch (HMDSettings->GraphicQuality)
	{
		// 高的质量的话
	case EGraphicQuality::EPIC:
		{
			//IdealRenderTargetSize = FIntPoint(4230, 2160);
			SrcBoxRight.right = 2160;
			SrcBoxRight.bottom = 2160;
			SrcBoxLeft.left = 2160;
			SrcBoxLeft.right = 4230;
			SrcBoxLeft.bottom = 2160;
		}
	case EGraphicQuality::High:
		{
			//IdealRenderTargetSize = FIntPoint(3840, 1920);
			SrcBoxRight.right = 1920;
			SrcBoxRight.bottom = 1920;
			SrcBoxLeft.left = 1920;
			SrcBoxLeft.right = 3840;
			SrcBoxLeft.bottom = 1920;
		}
		break;
		// 中等质量(75%)
	case EGraphicQuality::Medium:
		{
			//IdealRenderTargetSize = FIntPoint(2880, 1440);
			SrcBoxRight.right = 1440;
			SrcBoxRight.bottom = 1440;
			SrcBoxLeft.left = 1440;
			SrcBoxLeft.right = 2880;
			SrcBoxLeft.bottom = 1440;
		}
		break;
		// 低质量(50%)
	case EGraphicQuality::Low:
		{
			// 这个地方写错了，虽然是拷贝但是，是平铺的。所以不对
			//IdealRenderTargetSize = FIntPoint(2400, 1200);
			SrcBoxRight.right = 1200;
			SrcBoxRight.bottom = 1200;
			
			SrcBoxLeft.left = 1200;
			SrcBoxLeft.right = 2400;
			SrcBoxLeft.bottom = 1200;
		}
		break;
	default:
		;
	}
	// 右侧（3840*1920）
	SrcBoxRight.left = 0;
	SrcBoxRight.top = 0;
	SrcBoxRight.front = 0;
	SrcBoxRight.back = 1;
	// 左侧（3840*1920）
	SrcBoxLeft.top = 0;
	SrcBoxLeft.front = 0;
	SrcBoxLeft.back = 1;

	// 获取设备关联
	D3D11Device->GetImmediateContext(&D3D11DeviceContext);

	// FOV更新自服务时间
	OnFovUpdatedFromServiceEvent.BindRaw(this, &FPICOXRHMDDP::OnFovStateChanged);
	// 设置FOV更新事件
	FPICOXRDPManager::SetFovUpdatedFromServiceEvent(OnFovUpdatedFromServiceEvent);
	PXR_LOGD(PxrUnreal, "PXR_LivePreview start up finished!");
	return true;
}

// 终止
void FPICOXRHMDDP::Shutdown()
{
	FPICOXRDPManager::ShutDownLivePreview();
	//UnregisterSettings();
}

// fov状态更改
void FPICOXRHMDDP::OnFovStateChanged(const ps_common::DeviceFovInfo& FovInfo)
{
	BothFrustum.FovUp = FovInfo.up();
	BothFrustum.FovDown = FovInfo.down();
	BothFrustum.FovLeft = FovInfo.left();
	BothFrustum.FovRight = FovInfo.right();
	// 日志输出
	PXR_LOGD(PxrUnreal, "PXR_LivePreview OnFovStateChanged FovUp:%f FovDown:%f FovLeft:%f FovRight:%f!",
	         BothFrustum.FovUp,
	         BothFrustum.FovDown,
	         BothFrustum.FovLeft,
	         BothFrustum.FovRight
	);
}

//necessary, brush the rt on the Spectator screen, which is the window on the PC side
// 必要时，在旁观者屏幕上刷rt，这是PC侧的窗口
void FPICOXRHMDDP::CreateSpectatorScreenController()
{
	// 这是观察者的对象
	SpectatorScreenController = MakeUnique<FDefaultSpectatorScreenController>(this);
}

// 完整平的眼睛矩形
FIntRect FPICOXRHMDDP::GetFullFlatEyeRect_RenderThread(FTexture2DRHIRef EyeTexture) const
{
	static FVector2D SrcNormRectMin(0.05f, 0.2f);
	static FVector2D SrcNormRectMax(0.45f, 0.8f);
	return FIntRect(EyeTexture->GetSizeX() * SrcNormRectMin.X, EyeTexture->GetSizeY() * SrcNormRectMin.Y, EyeTexture->GetSizeX() * SrcNormRectMax.X, EyeTexture->GetSizeY() * SrcNormRectMax.Y);
}

// NOTE: 弃用函数 
void FPICOXRHMDDP::CopyTexture(FRHICommandList& RHICmdList, FTextureRHIRef SourceTexture, FTextureRHIRef DestTexture, FRHIGPUFence* Fence,bool bLeft,bool bUseRenderPass) const
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview SourceTexture Flag:%d",EnumHasAnyFlags(SourceTexture->GetFlags(),ETextureCreateFlags::SRGB));
	PXR_LOGD(PxrUnreal, "PXR_LivePreview DestTexture Flag:%d",EnumHasAnyFlags(DestTexture->GetFlags(),ETextureCreateFlags::SRGB));
	if (!bUseRenderPass)
	{
		RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
		RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::CopyDest));
	
		// source and dest are the same. simple copy
		if (bLeft)
		{
			RHICmdList.CopyTexture(SourceTexture, DestTexture, CopyInfoLeft);
		}
		else
		{
			RHICmdList.CopyTexture(SourceTexture, DestTexture, CopyInfoRight);
		}
	}
	else
	{
		IRendererModule* RendererModule1 = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

		RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVMask));
		RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

		// source and destination are different. rendered copy
		FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("PixelStreaming::CopyTexture"));
		{
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
			TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

			RHICmdList.SetViewport(0, 0, 0.0f, DestTexture->GetDesc().Extent.X, DestTexture->GetDesc().Extent.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
			// NOTE: 使用三线性采样 + 各向异性过滤
			//PixelShader->SetParameters(BatchedParameters, TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 8>::GetRHI(), SourceTexture);
			// NOTE: 采用双线性采样  
			PixelShader->SetParameters(BatchedParameters, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 0>::GetRHI(), SourceTexture);
			RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);

			FIntPoint TargetBufferSize(DestTexture->GetDesc().Extent.X, DestTexture->GetDesc().Extent.Y);
			if (bLeft)
			{
				RendererModule1->DrawRectangle(RHICmdList, 0, 0, // Dest X, Y
										 DestTexture->GetDesc().Extent.X,	// Dest Width
										 DestTexture->GetDesc().Extent.Y,	 // Dest Height
										  0.5, 0, // Source U, V
										  0.5, 1, // Source USize, VSize
										  TargetBufferSize, // Target buffer size
										  FIntPoint(1, 1), // Source texture size
										  VertexShader, EDRF_Default);
			}
			else
			{
				RendererModule1->DrawRectangle(RHICmdList, 0, 0, // Dest X, Y
										 DestTexture->GetDesc().Extent.X,	// Dest Width
										 DestTexture->GetDesc().Extent.Y,	// Dest Height
										  0, 0, // Source U, V
										  0.5, 1, // Source USize, VSize
										  TargetBufferSize, // Target buffer size
										  FIntPoint(1, 1), // Source texture size
										  VertexShader, EDRF_Default);
			}
		}

		RHICmdList.EndRenderPass();

		RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::SRVMask, ERHIAccess::CopySrc));
		RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::RTV, ERHIAccess::CopyDest));
	}

	if (Fence != nullptr)
	{
		RHICmdList.WriteGPUFence(Fence);
	}
}

// 渲染线程的拷贝纹理
void FPICOXRHMDDP::CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, FRHITexture2D* DstTexture, FIntRect DstRect, bool bClearBlack, bool bNoAlpha) const
{
	check(IsInRenderingThread());
	
	// 左右纹理2DRHI
	FRHITexture2D* LeftTexture2DRHI = pBridge->GetLeftSwapChain()->GetTexture2D();
	FRHITexture2D* RightTexture2DRHI = pBridge->GetRightSwapChain()->GetTexture2D();
	
	// PXR_RightPixelFormat/PXR_SrcTextureFormat: PF_R8G8B8A8             =37,
	// PXR_LOGD(PxrUnreal, "PXR_RightPixelFormat:%d",RightTexture2DRHI->GetDesc().Format);
	// NOTE: SrcTexture 的标识号为 9。RenderTargetable | ShaderResource  
	//PXR_LOGD(PxrUnreal, "PXR_SrcTextureCreateFlags:%d",SrcTexture->GetDesc().Flags);
	// NOTE: RightTexture2DRHI 的标识号为25。 RenderTargetable | ShaderResource | SRGB 
	// PXR_LOGD(PxrUnreal, "PXR_RightTextureCreateFlags:%d",RightTexture2DRHI->GetDesc().Flags);
	
	// 是否在流送
	if (FPICOXRDPManager::IsStreaming())
	{
		// 上锁互斥
		FPICOXRDPManager::LockKeyedMutex();
		if (D3D11DeviceContext)
		{
			D3D11DeviceContext->CopySubresourceRegion((ID3D11Resource *)LeftTexture2DRHI->GetNativeResource(), 0, 0, 0, 0, (ID3D11Resource *)SrcTexture->GetNativeResource(), 0, &SrcBoxLeft);
			D3D11DeviceContext->CopySubresourceRegion((ID3D11Resource *)RightTexture2DRHI->GetNativeResource(), 0, 0, 0, 0, (ID3D11Resource *)SrcTexture->GetNativeResource(), 0, &SrcBoxRight);
		}
		
		// 解锁Pico的互斥
		FPICOXRDPManager::UnlockKeyedMutex();
		
		uint32 SwapChainIndex = pBridge->GetLeftSwapChain()->GetSwapChainIndex_RHIThread();
		FPICOXRDPManager::SendMessage(SwapChainIndex);
	}
	
	const uint32 ViewportWidth = DstRect.Width();
	const uint32 ViewportHeight = DstRect.Height();
	const FIntPoint TargetSize(ViewportWidth, ViewportHeight);

	const float SrcTextureWidth = SrcTexture->GetSizeX();
	const float SrcTextureHeight = SrcTexture->GetSizeY();
	float U = 0.f, V = 0.f, USize = 1.f, VSize = 1.f;
	if (!SrcRect.IsEmpty())
	{
		U = SrcRect.Min.X / SrcTextureWidth;
		V = SrcRect.Min.Y / SrcTextureHeight;
		USize = SrcRect.Width() / SrcTextureWidth;
		VSize = SrcRect.Height() / SrcTextureHeight;
	}

	RHICmdList.Transition(FRHITransitionInfo(SrcTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));

	// #todo-renderpasses Possible optimization here - use DontLoad if we will immediately clear the entire target
	FRHIRenderPassInfo RPInfo(DstTexture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyTexture"));
	{
		if (bClearBlack)
		{
			const FIntRect ClearRect(0, 0, DstTexture->GetSizeX(), DstTexture->GetSizeY());
			RHICmdList.SetViewport(ClearRect.Min.X, ClearRect.Min.Y, 0, ClearRect.Max.X, ClearRect.Max.Y, 1.0f);
			DrawClearQuad(RHICmdList, FLinearColor::Black);
		}

		RHICmdList.SetViewport(DstRect.Min.X, DstRect.Min.Y, 0, DstRect.Max.X, DstRect.Max.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = bNoAlpha ? TStaticBlendState<>::GetRHI() : TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		const auto FeatureLevel = GMaxRHIFeatureLevel;
		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();

		const bool bSameSize = DstRect.Size() == SrcRect.Size();
		FRHISamplerState* PixelSampler = bSameSize ? TStaticSamplerState<SF_Point>::GetRHI() : TStaticSamplerState<SF_Bilinear>::GetRHI();
		
		if (EnumHasAnyFlags(SrcTexture->GetFlags(), TexCreate_SRGB))
		{
			TShaderMapRef<FScreenPSsRGBSource> PixelShader(ShaderMap);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
			PixelShader->SetParameters(BatchedParameters, PixelSampler, SrcTexture);
			
			RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
		}
		else
		{
			TShaderMapRef<FScreenPS> PixelShader(ShaderMap);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
			PixelShader->SetParameters(BatchedParameters, PixelSampler, SrcTexture);
			RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
		}

		RendererModule->DrawRectangle(
			RHICmdList,
			0, 0,
			ViewportWidth, ViewportHeight,
			U, V,
			USize, VSize,
			TargetSize,
			FIntPoint(1, 1),
			VertexShader,
			EDRF_Default);
	}
	RHICmdList.EndRenderPass();
}

// NOTE: 这个控制是导致画面的关键
void FPICOXRHMDDP::RenderTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
	check(IsInRenderingThread());
	// PXR_LOGD(PxrUnreal,"BackBufferSize->X:%d,Y:%d",BackBuffer->GetSizeX(),BackBuffer->GetSizeY());
	// PXR_LOGD(PxrUnreal,"SrcSize->X:%d,Y:%d",SrcTexture->GetSizeX(),SrcTexture->GetSizeY());
	// PXR_LOGD(PxrUnreal,"WindowSize->X:%f,Y:%f",WindowSize.X,WindowSize.Y);
	
	if (bSplashIsShown || !IsBackgroundLayerVisible())
	{
		FRHIRenderPassInfo RPInfo(SrcTexture, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("Clear"));
		{
			DrawClearQuad(RHICmdList, FLinearColor(0, 0, 0, 0));
		}
		RHICmdList.EndRenderPass();
	}
	check(SpectatorScreenController);
	// 这个Srctexture需要变成一个临时的拷贝对象
	//SpectatorScreenController->SetSpectatorScreenMode(ESpectatorScreenMode::SingleEyeCroppedToFill);
	SpectatorScreenController->RenderSpectatorScreen_RenderThread(RHICmdList, BackBuffer, SrcTexture, WindowSize);
	
}

void FPICOXRHMDDP::PostRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
}

bool FPICOXRHMDDP::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return GEngine && GEngine->IsStereoscopic3D(Context.Viewport);
}


void FPICOXRHMDDP::BridgeBaseImpl::BeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList)
{}

void FPICOXRHMDDP::BridgeBaseImpl::BeginRendering_RHI()
{
	check(!IsRunningRHIInSeparateThread() || IsInRHIThread());
}

void FPICOXRHMDDP::BridgeBaseImpl::CreateSwapChain(const FTextureRHIRef& BindingTexture, TArray<FTextureRHIRef>&& SwapChainTextures)
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview CreateSwapChain!");
	check(IsInRenderingThread());
	check(SwapChainTextures.Num());
	SwapChain = CreateXRSwapChain(MoveTemp(SwapChainTextures), BindingTexture);
}

void FPICOXRHMDDP::BridgeBaseImpl::CreateLeftSwapChain(const FTextureRHIRef& BindingTexture, TArray<FTextureRHIRef>&& SwapChainTextures)
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview CreateLeftSwapChain!");
	check(IsInRenderingThread());
	check(SwapChainTextures.Num());
	LeftSwapChain = CreateXRSwapChain(MoveTemp(SwapChainTextures), BindingTexture);
}

void FPICOXRHMDDP::BridgeBaseImpl::CreateRightSwapChain(const FTextureRHIRef& BindingTexture, TArray<FTextureRHIRef>&& SwapChainTextures)
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview CreateRightSwapChain!");
	check(IsInRenderingThread());
	check(SwapChainTextures.Num());
	RightSwapChain = CreateXRSwapChain(MoveTemp(SwapChainTextures), BindingTexture);
}

bool FPICOXRHMDDP::BridgeBaseImpl::Present(int& SyncInterval)
{
	//This  must return true。
	check(IsRunningRHIInSeparateThread() ? IsInRHIThread() : IsInRenderingThread());

	//necessary, brush the RT to steam
	FinishRendering();
	// Increment swap chain index post-swap.
	SwapChain->IncrementSwapChainIndex_RHIThread();
	if (FPICOXRDPManager::IsStreaming())
	{
		GetLeftSwapChain()->IncrementSwapChainIndex_RHIThread();
		GetRightSwapChain()->IncrementSwapChainIndex_RHIThread();
	}
	

	SyncInterval = 0;

	return true;
}

bool FPICOXRHMDDP::BridgeBaseImpl::NeedsNativePresent()
{
	//This return value does not affect the PC display
	return true;
}

void FPICOXRHMDDP::BridgeBaseImpl::PostPresent()
{
}

FPICOXRHMDDP::D3D11Bridge::D3D11Bridge(FPICOXRHMDDP* plugin)
	: BridgeBaseImpl(plugin)
{
}

//necessary, brush the RT to steam
void FPICOXRHMDDP::D3D11Bridge::FinishRendering()
{
}


void FPICOXRHMDDP::D3D11Bridge::Reset()
{
}

void FPICOXRHMDDP::D3D11Bridge::UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI)
{
}

#endif // STEAMVR_SUPPORTED_PLATFORMS
