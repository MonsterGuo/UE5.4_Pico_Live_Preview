//Unreal® Engine, Copyright 1998 – 2023, Epic Games, Inc. All rights reserved.

#pragma once
#include "IPXR_DPModule.h"
#include "HardwareInfo.h"

#if 1

#include "HeadMountedDisplay.h"
#include "HeadMountedDisplayBase.h"
#include "IStereoLayers.h"			// 立体层
#include "StereoLayerManager.h"		// 立体层管理器
#include "XRRenderTargetManager.h"	// XR渲染目标哦管理器
#include "XRRenderBridge.h"			// 渲染桥
#include "XRSwapChain.h"			// XR交换链
#include "IHeadMountedDisplayVulkanExtensions.h"
#include "SceneViewExtension.h"		// 场景视图拓展

#include "PXR_DPManager.h"

class IRendererModule;

// 眼睛类型
enum class EEyeType
{
	EyeLeft = 0,
	EyeRight = 1,
	EyeBoth = 2,
};

// PICO矩阵DP
struct FPICOXRFrustumDP
{
	float FovLeft;		// 左侧FOV
	float FovRight;		// 右侧FOV
	float FovUp;		// 上FOV
	float FovDown;		// 下FOV
	float Near;			// 近FOV
	float Far;			// 远FOV
	EEyeType Type;

	// 默认构造函数
	FPICOXRFrustumDP()
	{
		FovUp = 0.907f;
		FovDown = -0.907f;
		FovLeft = -0.907f;
		FovRight = 0.907f;
		Near = 0.0508f;
		Far = 100;
		Type=EEyeType::EyeBoth;
	}
	// 转换成字符串
	FString ToString() const
	{
		return
			TEXT(" FPICOXRFrustum Left : ") + FString::SanitizeFloat(FovLeft) +
			TEXT(" FPICOXRFrustum Right : ") + FString::SanitizeFloat(FovRight) +
			TEXT(" FPICOXRFrustum Up : ") + FString::SanitizeFloat(FovUp) +
			TEXT(" FPICOXRFrustum Down : ") + FString::SanitizeFloat(FovDown) +
			TEXT(" FPICOXRFrustum H : ") + FString::SanitizeFloat(FovRight - FovLeft) +
			TEXT(" FPICOXRFrustum V : ") + FString::SanitizeFloat(FovUp - FovDown) +
			TEXT(" FPICOXRFrustum Near : ") + FString::SanitizeFloat(Near) +
			TEXT(" FPICOXRFrustum Far : ") + FString::SanitizeFloat(Far);
	}
};

/** Stores vectors, in clockwise order, to define soft and hard bounds for Chaperone */
// NOTE： 按顺时针顺序存储向量，以定义伴侣的软边界和硬边界 
struct FBoundingQuad
{
	FVector Corners[4];
};

/**
 * Struct for managing stereo layer data.
 * NOTE： 结构，用于管理立体图层数据。 
 */
struct FPICODPLayer
{
	// 立体层描述的别名
	typedef IStereoLayers::FLayerDesc FLayerDesc;
	FLayerDesc	          LayerDesc;		// 层描述
	bool				  bUpdateTexture;	// 是否更新纹理
	// 构造函数
	FPICODPLayer(const FLayerDesc& InLayerDesc)
		: LayerDesc(InLayerDesc)
		, bUpdateTexture(false)
	{}

	// Required by TStereoLayerManager:
	// 设置层ID
	void SetLayerId(uint32 InId) { LayerDesc.SetLayerId(InId); }
	// 获取层ID
	uint32 GetLayerId() const { return LayerDesc.GetLayerId(); }
	// 获取层描述成员
	friend bool GetLayerDescMember(const FPICODPLayer& Layer, FLayerDesc& OutLayerDesc);
	// 设置层描述成员
	friend void SetLayerDescMember(FPICODPLayer& Layer, const FLayerDesc& InLayerDesc);
	// 标记层纹理的更新
	friend void MarkLayerTextureForUpdate(FPICODPLayer& Layer);
};

/**
 * PICODP Head Mounted Display public FPICODPAssetManager,
 * PICODP 头戴式显示器 继承自 PICODP资源管理器
 */
class FPICOXRHMDDP : public FHeadMountedDisplayBase, public FXRRenderTargetManager, public TStereoLayerManager<FPICODPLayer>, public FHMDSceneViewExtension
{
public:
	/** Constructor 函数*/
	FPICOXRHMDDP(const FAutoRegister&, IPICOXRDPModule*);

	/** Destructor */
	virtual ~FPICOXRHMDDP();

	/** @return	True if the API was initialized OK */
	// 是否初始化了
	bool IsInitialized() const;
	

	// 系统名称
	static const FName SystemName;
	/** IXRTrackingSystem interface */
	// NOTE: 追踪系统的接口 
	// 获取系统名称
	virtual FName GetSystemName() const override
	{
		return SystemName;
	}
	// 获取XR系统标识
	virtual int32 GetXRSystemFlags() const override
	{
		return EXRSystemFlags::IsHeadMounted;
	}
	// 获取版本字符串
	virtual FString GetVersionString() const override;

	// 获取头戴设备的类型指针
	virtual class IHeadMountedDisplay* GetHMDDevice() override
	{
		return this;
	}

	// 获取立体渲染的设备名称
	virtual class TSharedPtr< class IStereoRendering, ESPMode::ThreadSafe > GetStereoRenderingDevice() override
	{
		return SharedThis(this);
	}

	// 在开始游戏的帧时调用
	virtual bool OnStartGameFrame(FWorldContext& WorldContext) override;
	//是否支持位置追踪
	virtual bool DoesSupportPositionalTracking() const override;
	// 是否追踪了位置信息
	virtual bool HasValidTrackingPosition() override;
	// 枚举追踪的设备
	virtual bool EnumerateTrackedDevices(TArray<int32>& TrackedIds, EXRTrackedDeviceType DeviceType = EXRTrackedDeviceType::Any) override;

	// 获取追踪传感器参数
	virtual bool GetTrackingSensorProperties(int32 InDeviceId, FQuat& OutOrientation, FVector& OutOrigin, FXRSensorProperties& OutSensorProperties) override;
	// 获取追踪设备参数序列号
	virtual FString GetTrackedDevicePropertySerialNumber(int32 DeviceId) override;
	// 获取当前的位置
	virtual bool GetCurrentPose(int32 DeviceId, FQuat& CurrentOrientation, FVector& CurrentPosition) override;
	// 获取相对的位置
	virtual bool GetRelativeEyePose(int32 DeviceId,int32 ViewIndex, FQuat& OutOrientation, FVector& OutPosition) override;
	// 是否追踪
	virtual bool IsTracking(int32 DeviceId) override;

	// 重置旋转和位置
	virtual void ResetOrientationAndPosition(float yaw = 0.f) override;
	// 重置旋转
	virtual void ResetOrientation(float Yaw = 0.f) override;
	// 重置位置
	virtual void ResetPosition() override;

	// 设置基础的旋转
	virtual void SetBaseRotation(const FRotator& BaseRot) override;
	// 获取基础的旋转
	virtual FRotator GetBaseRotation() const override;
	// 设置基础的旋转参数
	virtual void SetBaseOrientation(const FQuat& BaseOrient) override;
	// 获取基础的旋转参数
	virtual FQuat GetBaseOrientation() const override;
	// 设置基础的位置
	virtual void SetBasePosition(const FVector& BasePosition) override;
	// 获取基础位置
	virtual FVector GetBasePosition() const override;
	// 开始
	virtual void OnBeginPlay(FWorldContext& InWorldContext) override;
	// 结束
	virtual void OnEndPlay(FWorldContext& InWorldContext) override;
	// 记录分析
	virtual void RecordAnalytics() override;
	// 设置追踪原点
	virtual void SetTrackingOrigin(EHMDTrackingOrigin::Type NewOrigin) override;
	// 获取追踪原点
	virtual EHMDTrackingOrigin::Type GetTrackingOrigin() const override;
	// 眼动追踪变换
	virtual bool GetFloorToEyeTrackingTransform(FTransform& OutFloorToEye) const override;
	// 获取玩家边界
	virtual FVector2D GetPlayAreaBounds(EHMDTrackingOrigin::Type Origin) const override;
	// 拷贝纹理
	void CopyTexture(FRHICommandList& RHICmdList, FTextureRHIRef SourceTexture, FTextureRHIRef DestTexture, FRHIGPUFence* Fence,bool bLeft,bool bUseRenderPass) const;

public:
	/** IHeadMountedDisplay interface */
	// NOTE: 头戴显示器接口
	// 是否链接
	virtual bool IsHMDConnected() override;
	// 是否启用
	virtual bool IsHMDEnabled() const override;
	// 获取HMD的穿戴状态
	virtual EHMDWornState::Type GetHMDWornState() override;
	// 启用HMD吗
	virtual void EnableHMD(bool allow = true) override;
	// 获取HMD显示器信息
	virtual bool GetHMDMonitorInfo(MonitorInfo&) override;

	// 获取FOV
	virtual void GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees) const override;
	// 设置瞳距
	virtual void SetInterpupillaryDistance(float NewInterpupillaryDistance) override;
	// 获取瞳距
	virtual float GetInterpupillaryDistance() const override;
	// 色度校正是否启用
	virtual bool IsChromaAbCorrectionEnabled() const override;
	// 更新屏幕设置
	virtual void UpdateScreenSettings(const FViewport* InViewport) override {}

	// 获取HMD失真是否启用
	virtual bool GetHMDDistortionEnabled(EShadingPath ShadingPath) const override;

	// 开始渲染调用
	virtual void OnBeginRendering_GameThread() override;
	// 开始渲染
	virtual void OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily) override;

	// 获取像素密度
	virtual float GetPixelDenity() const override { return 1; }
	// 设置像素密度
	virtual void SetPixelDensity(const float NewDensity) override { PixelDensity = NewDensity; }
	// 获取理想渲染目标尺寸 
	virtual FIntPoint GetIdealRenderTargetSize() const override { return IdealRenderTargetSize; }

	//PICO Preview++++++++++++++++++++++
	/** IStereoRendering interface */
	// 是否启用立体
	virtual bool IsStereoEnabled() const override;
	// 启用立体
	virtual bool EnableStereo(bool stereo = true) override;
	// 调整视口矩形
	virtual void AdjustViewRect(int32 ViewIndex, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const override;
	// 计算立体视口偏移
	virtual void CalculateStereoViewOffset(const int32 ViewIndex, FRotator& ViewRotation, const float MetersToWorld, FVector& ViewLocation) override;
	// 获取立体投影矩阵
	virtual FMatrix GetStereoProjectionMatrix(const int32 ViewIndex) const override;
	// 渲染纹理
	virtual void RenderTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const override;
	//virtual void GetEyeRenderParams_RenderThread(const FRenderingCompositePassContext& Context, FVector2D& EyeToSrcUVScaleValue, FVector2D& EyeToSrcUVOffsetValue) const override;
	// 获取渲染慕目标的管理器
	virtual IStereoRenderTargetManager* GetRenderTargetManager() override { return this; }

	/** FXRRenderTargetManager interface */
	// 获取激活渲染器桥
	virtual FXRRenderBridge* GetActiveRenderBridge_GameThread(bool bUseSeparateRenderTarget) override;
	// 是否使用分开渲染目标
	virtual bool ShouldUseSeparateRenderTarget() const override
	{
		check(IsInGameThread());
		return IsStereoEnabled();
	}
	// 计算渲染目标的尺寸
	virtual void CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY) override;
	// 需要重新分配视口渲染目标
	virtual bool NeedReAllocateViewportRenderTarget(const class FViewport& Viewport) override;
	//virtual bool AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags InTexFlags, ETextureCreateFlags InTargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples = 1) override;
	// 分配渲染目标纹理
	virtual bool AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples = 1) override;
	// 是否拷贝调试层到观众屏幕
	virtual bool ShouldCopyDebugLayersToSpectatorScreen() const override { return true; }
	//PICO Preview-----------------------
	//ISceneViewExtension interface
	// NOTE：场景视口拓展接口
	// 启动视口家族
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	// 启用视口
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	// 开始渲染视口家族
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	// 渲染视口之前
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {}
	// 渲染视口家族之前
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override {};
	// 渲染视口之后
	virtual void PostRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override;
	// PICO Preview ！！！
	// NOTE: PICO预览
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//FPICOXRDPManagerPtr CurrentDPManager = NULL;
	// SpectatorScreen
	//PICO Preview++++++++++++++++++++++++
private:
	// 创建观众屏幕控制器
	void CreateSpectatorScreenController();
public:
	// 获取完整的平的眼睛矩形
	virtual FIntRect GetFullFlatEyeRect_RenderThread(FTexture2DRHIRef EyeTexture) const override;
	// 拷贝纹理
	virtual void CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, FRHITexture2D* DstTexture, FIntRect DstRect, bool bClearBlack, bool bNoAlpha) const override;
	//PICO Preview-------------------
	// NOTE：PICO预览
	class BridgeBaseImpl : public FXRRenderBridge
	{
	public:
		// 桥基础实现
		BridgeBaseImpl(FPICOXRHMDDP* plugin)
			: Plugin(plugin)					// 插件
			, bInitialized(false)				// 是否初始化
			, bUseExplicitTimingMode(false)		// 是否使用现式的时间模式
		{}

		// Render bridge virtual interface
		// 渲染桥虚拟接口
		virtual bool Present(int& SyncInterval) override;
		// 预设之后
		virtual void PostPresent() override;
		// 需要原生的预设
		virtual bool NeedsNativePresent() override;

		// Non-virtual public interface
		// 非虚拟的公开接口
		// 是否初始化
		bool IsInitialized() const { return bInitialized; }

		// 是否使用显式的时间模式
		bool IsUsingExplicitTimingMode() const
		{
			return bUseExplicitTimingMode;
		}

		// 获取交换链
		FXRSwapChainPtr GetSwapChain() { return SwapChain; }
		// 获取左交换链
		FXRSwapChainPtr GetLeftSwapChain() { return LeftSwapChain; }
		// 获取右交换链
		FXRSwapChainPtr GetRightSwapChain() { return RightSwapChain; }
		// 获取深度交换链
		FXRSwapChainPtr GetDepthSwapChain() { return DepthSwapChain; }

		/** Schedules BeginRendering_RHI on the RHI thread when in explicit timing mode */
		// NOTE: 开始渲染
		void BeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList);

		/** Called only when we're in explicit timing mode, which needs to be paired with a call to PostPresentHandoff */
		// NOTE: 开始渲染——RHI
		void BeginRendering_RHI();

		// 创建交换链
		void CreateSwapChain(const FTextureRHIRef& BindingTexture, TArray<FTextureRHIRef>&& SwapChainTextures);
		// 创建左交换链
		void CreateLeftSwapChain(const FTextureRHIRef& BindingTexture, TArray<FTextureRHIRef>&& SwapChainTextures);
		// 创建右交换链
		void CreateRightSwapChain(const FTextureRHIRef& BindingTexture, TArray<FTextureRHIRef>&& SwapChainTextures);

		// Virtual interface implemented by subclasses
		// 通过子类实现
		virtual void Reset() = 0;

	private:
		// 计数渲染
		virtual void FinishRendering() = 0;

	protected:
		
		FPICOXRHMDDP*  Plugin;					// 插件
		FXRSwapChainPtr			SwapChain;
		FXRSwapChainPtr			LeftSwapChain;
		FXRSwapChainPtr			RightSwapChain;

		FXRSwapChainPtr			DepthSwapChain;

		bool					bInitialized;

		/** If we use explicit timing mode, we must have matching calls to BeginRendering_RHI and PostPresentHandoff */
		bool					bUseExplicitTimingMode;

	};

#if PLATFORM_WINDOWS
	// D3D11的桥
	class D3D11Bridge : public BridgeBaseImpl
	{
	public:
		D3D11Bridge(FPICOXRHMDDP* plugin);
		/// <summary>
		/// Brush the RT to the runtime of steam
		virtual void FinishRendering() override;
		// 更新视口
		virtual void UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI) override;
		virtual void Reset() override;
		TRefCountPtr<ID3D11Texture2D> intexture;

	};
#endif // PLATFORM_WINDOWS
protected:
	// 获取世界元素缩放
	virtual float GetWorldToMetersScale() const override;


private:

	/**
	 * Starts up the OpenVR API. Returns true if initialization was successful, false if not.
	 * 开始OpenVR接口，返回真，如果初始化成功了，不然为假
	 */
	bool Startup();

	/**
	 * Shuts down the OpenVR API
	 * 结束OpenVR接口
	 */
	void Shutdown();

	// 注册设置
	void RegisterSettings();
	// 取消注册接口
	void UnregisterSettings();


private:
	// HMD设置
	class UPICOXRDPSettings* HMDSettings;

	
	// 双视锥
	FPICOXRFrustumDP BothFrustum;

	// 拷贝信息左右
	FRHICopyTextureInfo CopyInfoLeft;
	FRHICopyTextureInfo CopyInfoRight;
	// DX11设备
	ID3D11Device*  D3D11Device  = nullptr;
	// DX11设备上下文
	ID3D11DeviceContext* D3D11DeviceContext = nullptr;
	// 荧幕盒子组偶有
	D3D11_BOX SrcBoxLeft;
	D3D11_BOX SrcBoxRight;
	// 是否为VR 预览
	bool bIsVRPreview;
	// 初始化是否成功
	bool InitializedSucceeded;
	// 头戴现实器是否启用
	bool bHmdEnabled;
	// 穿戴状态
	EHMDWornState::Type HmdWornState;
	// 是否期待双眼立体
	bool bStereoDesired;
	// 立体是否启用
	bool bStereoEnabled;
	// 是否遮蔽模型构建
	bool bOcclusionMeshesBuilt;
	// FOV更新从服务额事件
	FOnFovUpdatedFromServiceEvent OnFovUpdatedFromServiceEvent;

	// FOV状态更改
	void OnFovStateChanged(const ps_common::DeviceFovInfo& FovInfo);

	// Current world to meters scale. Should only be used when refreshing poses.
	// Everywhere else, use the current tracking frame's WorldToMetersScale.
	// 游戏世界单位缩放
	float GameWorldToMetersScale;

	// 隐藏区域模型
	FHMDViewMesh HiddenAreaMeshes[2];
	// 可见模型区域
	FHMDViewMesh VisibleAreaMeshes[2];



	// 窗口镜像边界宽度
	uint32 WindowMirrorBoundsWidth;
	// 窗口镜像边界高度
	uint32 WindowMirrorBoundsHeight;

	// 理想的渲染目标尺寸
	FIntPoint IdealRenderTargetSize;
	// 像素密度
	float PixelDensity;

	/** How far the HMD has to move before it's considered to be worn */
	// HMD移动阈值
	float HMDWornMovementThreshold;

	/** used to check how much the HMD has moved for changing the Worn status */
	// HMD开始位置
	FVector					HMDStartLocation;

	// HMD base values, specify forward orientation and zero pos offset
	// 基础旋转，基础偏移
	FQuat					BaseOrientation;	// base orientation
	FVector					BaseOffset;

	// State for tracking quit operation
	// 追踪是否退出的状态
	bool					bIsQuitting;
	double					QuitTimestamp;

	/**  True if the HMD sends an event that the HMD is being interacted with */
	// 如果HMD发送与HMD交互的事件，则为True
	bool					bShouldCheckHMDPosition;

	// 渲染模块
	IRendererModule* RendererModule;
	// PICODP插件，其实就是预览模块
	IPICOXRDPModule* PICODPPlugin;

	// 显示ID
	FString DisplayId;
	// 播放器旋转
	FQuat PlayerOrientation;
	// 播放器位置
	FVector PlayerLocation;
	// 桥
	TRefCountPtr<BridgeBaseImpl> pBridge;
};

#endif //STEAMVR_SUPPORTED_PLATFORMS
