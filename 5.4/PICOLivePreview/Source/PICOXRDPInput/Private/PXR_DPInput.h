//Unreal® Engine, Copyright 1998 – 2023, Epic Games, Inc. All rights reserved.

#pragma once
#include "GenericPlatform/IInputInterface.h"
#include "XRMotionControllerBase.h"
#include "InputDevice.h"
#include "IHapticDevice.h"
#include "InputMappingContext.h"
#include "PICOXRDPHMD/Private/PXR_DPHMD.h"

#define ButtonEventNum 12
// NOTE: PICO按键类型（这个是一样的） 
struct EPICOButton
{
	enum Type
	{
		RockerX,
		RockerY,
		Home,
		App,
		Rocker,
		VolumeUp,
		VolumeDown,
		Trigger,
		Power,
		AorX,
		BorY,
		Grip,
		RockerUp,
		RockerDown,
		RockerLeft,
		RockerRight,
		ButtonCount
	};
};

// 这个暂时没用到
struct EPICOHandButton
{
	enum Type
	{
		Pinch,
		ButtonCount
	};
};

// NOTE:触摸按钮类型
struct EPICOTouchButton
{
	enum Type
	{
		AorX,		// A或者X的触摸
		BorY,		// B或者Y的触摸
		Rocker,		// 侧键的触摸
		Trigger,	// Trigger扳机的触摸 3
		Thumbrest,	// 侧面
        ButtonCount
    };
};

// 控制器的手柄
struct EPICOXRControllerHandness
{
	enum Type
	{
		LeftController,
		RightController,
		ControllerCount
	};
};

// 输入类型？ 为啥没有PICO4奇怪
enum  EPICOInputType:uint8
{
	Unknown = 0,
	G2      = 3,
	Neo2    = 4,
	Neo3    = 5,
};

// 触摸按键的状态
enum  ETouchButtonStatus:uint16
{
	None = 0,
	XATouch = 1<<1,
	YBTouch = 1<<3,
	JoystickTouch = 1<<5,
	TriggerTouch = 1<<7,
	// GripTouch = 512,
	KBlankTouch = 1<<13,
};

// 按键的状态
enum class EButtonStatus:uint16
{
	kNone = 0,
	kXAClick =1<< 0,
	kXATouch =1<< 1,
	kYBClick =1<< 2,
	kYBTouch =1<< 3,

	
	kJoystickClick =1<< 4,
	kJoystickTouch =1<< 5,
	
	kTriggerClick =1<< 6,
	kTriggerTouch =1<< 7,

	kHomeClick =1<< 8,
	kGripClick =1<< 9,
	
	kMenuButtonClick =1<< 12,

	kThumbrestTouch =1<< 13,
};

enum class EConnectionStatus
{
	kNotInitialized = 0,
	kDisconnected = 1,
	kConnected = 2,
	kConnecting = 3,
	kError = 4
};

//class FPICOXRHMD;
class FPICOXRDPInput :public IInputDevice,public FXRMotionControllerBase,public IHapticDevice,public TSharedFromThis<FPICOXRDPInput>
{
public:
	FPICOXRDPInput();
	virtual ~FPICOXRDPInput();

public:
	
	// IInputDevice overrides
	virtual void Tick(float DeltaTime) override;
	virtual void SendControllerEvents() override;
	
	virtual void SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) override;
	
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;

	// 不用更改
	virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues &values) override;

	// IMotionController overrides
	virtual FName GetMotionControllerDeviceTypeName() const override;
	virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
	virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const override;

	// IHapticDevice overrides
	IHapticDevice* GetHapticDevice() override { return (IHapticDevice*)this; }
	// 需要更新
	virtual void SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues& Values) override;
	// NOTE:一致的
	virtual void GetHapticFrequencyRange(float& MinFrequency, float& MaxFrequency) const override;
	// NOTE: 一致的 
	virtual float GetHapticAmplitudeScale() const override;

	/*FPICOXRHMD* GetPICOXRHMD();*/
	int32 UPxr_GetControllerPower(int32 Handness);
	// NOTE： 这个一样
	bool UPxr_GetControllerConnectState(int32 Handness);
	// NOTE： 这个一样
	bool UPxr_GetControllerMainInputHandle(int32& Handness);
	// NOTE: 稍微更改
	bool UPxr_SetControllerMainInputHandle(int32 inHandness);

	void SetHeadPosition(FVector Position);
	void SetHeadOrientation(FQuat Orientation);
	
	static FVector OriginOffsetL;
	static FVector OriginOffsetR;

	static void RegisterKeys();
	void BuildActions();
	
private:

	void SetKeyMapping();
	void ProcessButtonEvent();
	void ProcessButtonAxis();
	void ProcessEnhancedInput();
	bool GetPicoButtonState(FKey InKey);
	static void AddNonExistingKey(const TArray<FKey> &ExistAllKeys,const FKeyDetails& KeyDetails);

	FPICOXRHMDDP* PICOXRHMD=nullptr;
	TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
	bool LeftConnectState;
	bool RightConnectState;
	FName Buttons[(int32)EPICOXRControllerHandness::ControllerCount][(int32)EPICOButton::ButtonCount];
	FName TouchButtons[(int32)EPICOXRControllerHandness::ControllerCount][(int32)EPICOTouchButton::ButtonCount];
	int32 LastLeftControllerButtonState[EPICOButton::ButtonCount] = { 0 };
	int32 LastRightControllerButtonState[EPICOButton::ButtonCount] = { 0 };
	int32 LastLeftTouchButtonState[EPICOTouchButton::ButtonCount] = { 0 };
	int32 LastRightTouchButtonState[EPICOTouchButton::ButtonCount] = { 0 };
	int32 LeftControllerPower;
	int32 RightControllerPower;
	bool bLeftButtonPressed = false;
	bool bRightButtonPressed = false;
	FVector2D LeftControllerTouchPoint;
	FVector2D RightControllerTouchPoint;
	float LeftControllerTriggerValue;
	float RightControllerTriggerValue;
	float LeftControllerGripValue;
	float RightControllerGripValue;
	uint32_t MainControllerHandle;
	FVector SourcePosition;
	FQuat SourceOrientation;
	EPICOInputType ControllerType;
	bool IsEnhancedInput;
	TMap<TStrongObjectPtr<const UInputMappingContext>, uint32> InputMappingContextToPriorityMap;
};


