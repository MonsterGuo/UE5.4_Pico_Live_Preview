//Unreal® Engine, Copyright 1998 – 2023, Epic Games, Inc. All rights reserved.

#include "PXR_DPInput.h"
#include "PXR_DPInputState.h"
#include "CoreMinimal.h"
#include "IXRTrackingSystem.h"
#include "EnhancedInputDeveloperSettings.h"

#include "EnhancedInputLibrary.h"
#include "EnhancedInputSubsystemInterface.h"
#include "EnhancedInputModule.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "PlayerMappableInputConfig.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Engine/Engine.h"

#include "PXR_Log.h"


#define LOCTEXT_NAMESPACE "PICOXRDPInput"

FVector FPICOXRDPInput::OriginOffsetL = FVector::ZeroVector;
FVector FPICOXRDPInput::OriginOffsetR = FVector::ZeroVector;

FPICOXRDPInput::FPICOXRDPInput()
	:MessageHandler(new FGenericApplicationMessageHandler())		// OK
	, LeftConnectState(false)
	, RightConnectState(false)
	, LeftControllerPower(0)
	, RightControllerPower(0)
	, LeftControllerTouchPoint(FVector2D::ZeroVector)
	, RightControllerTouchPoint(FVector2D::ZeroVector)
	, LeftControllerTriggerValue(0.0f)
	, RightControllerTriggerValue(0.0f)
	, LeftControllerGripValue(0.0f)
	, RightControllerGripValue(0.0f)
	, MainControllerHandle(-1)
	, SourcePosition(FVector::ZeroVector)
	, SourceOrientation(FQuat::Identity)
	, ControllerType(EPICOInputType::Unknown)
{
	SetKeyMapping();
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	if (PICOXRHMD == nullptr)
	{
		static FName SystemName(TEXT("PICOXRPreview"));
		if (GEngine)
		{
			if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
			{
				PICOXRHMD = static_cast<FPICOXRHMDDP*>(GEngine->XRSystem.Get());
				
			}
		}
	}
	IsEnhancedInput = FModuleManager::Get().IsModuleLoaded("EnhancedInput");
	BuildActions();
}

FPICOXRDPInput::~FPICOXRDPInput()
{
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}


void FPICOXRDPInput::Tick(float DeltaTime)
{
	// NOTE: 获取 
	if (PICOXRHMD&&FPICOXRDPManager::IsStreaming())
	{
		// 左侧链接状态
		LeftConnectState = FPICOXRDPManager::GetControllerConnectionStatus(EControllerHand::Left);
		// 右侧链接状态
		RightConnectState = FPICOXRDPManager::GetControllerConnectionStatus(EControllerHand::Right);
	}
	
}

// NOTE: 发送控制器状态()
void FPICOXRDPInput::SendControllerEvents()
{
	// NOTE: 这里写的是常规输入的状态，并不是增强输入 
	ProcessButtonEvent();
	ProcessButtonAxis();
	//输入设备管理器
	
	
 }  

// 发送消息的句柄
void FPICOXRDPInput::SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
{
	MessageHandler = InMessageHandler;
}

// 执行事件，这里是正确的，因为不需要额外处理了
bool FPICOXRDPInput::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	return false;
}

// 设置通道的值
void FPICOXRDPInput::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
{
	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	FPlatformUserId InPlatformUser = FGenericPlatformMisc::GetPlatformUserForUserIndex(ControllerId);
	FInputDeviceId InDeviceId = INPUTDEVICEID_NONE;
	DeviceMapper.RemapControllerIdToPlatformUserAndDevice(ControllerId, InPlatformUser, InDeviceId);
	

	switch (ChannelType)
	{
	case FForceFeedbackChannelType::LEFT_LARGE:
	case FForceFeedbackChannelType::LEFT_SMALL:
	{
#if PLATFORM_ANDROID
		Pxr_SetControllerVibration(0, Value, 10);
#endif
		break;
	}
	case FForceFeedbackChannelType::RIGHT_LARGE:
	case FForceFeedbackChannelType::RIGHT_SMALL:
	{
#if PLATFORM_ANDROID
		Pxr_SetControllerVibration(1, Value, 10);
#endif
		break;

	}
	default:
		break;
	}

}

// 这个没啥问题
void FPICOXRDPInput::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& values)
{
	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	FPlatformUserId InPlatformUser = FGenericPlatformMisc::GetPlatformUserForUserIndex(ControllerId);
	FInputDeviceId InDeviceId = INPUTDEVICEID_NONE;
	DeviceMapper.RemapControllerIdToPlatformUserAndDevice(ControllerId, InPlatformUser, InDeviceId);

	if (values.LeftLarge > 0)
	{
#if PLATFORM_ANDROID
		Pxr_SetControllerVibration(0, values.LeftLarge, 10);
#endif
	}
	if (values.RightLarge > 0)
	{
#if PLATFORM_ANDROID
		Pxr_SetControllerVibration(1, values.RightLarge, 10);
#endif
	}

}

// 这个没啥问题
FName FPICOXRDPInput::GetMotionControllerDeviceTypeName() const
{
	return FName(TEXT("PICOXRDPInput"));
}

// 获取控制器的旋转和位置 NOTE:这个应该是不用看
bool FPICOXRDPInput::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
	EControllerHand DeviceHand;
	GetHandEnumForSourceName(MotionSource, DeviceHand);

	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	FPlatformUserId InPlatformUser = FGenericPlatformMisc::GetPlatformUserForUserIndex(ControllerIndex);
	FInputDeviceId InDeviceId = INPUTDEVICEID_NONE;
	DeviceMapper.RemapControllerIdToPlatformUserAndDevice(ControllerIndex, InPlatformUser, InDeviceId);
	
	if (PICOXRHMD && FPICOXRDPManager::IsStreaming())
	{
		if (DeviceHand == EControllerHand::Left)
		{
			FPICOXRDPManager::GetControllerPositionAndRotation(DeviceHand, WorldToMetersScale, OutPosition, OutOrientation);
			return true;
		}
		if (DeviceHand == EControllerHand::Right)
		{
			FPICOXRDPManager::GetControllerPositionAndRotation(DeviceHand, WorldToMetersScale, OutPosition, OutOrientation);
			return true;
		}
	}
	return false;
}

ETrackingStatus FPICOXRDPInput::GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const
{
	EControllerHand DeviceHand;
	GetHandEnumForSourceName(MotionSource, DeviceHand);

	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	FPlatformUserId InPlatformUser = FGenericPlatformMisc::GetPlatformUserForUserIndex(ControllerIndex);
	FInputDeviceId InDeviceId = INPUTDEVICEID_NONE;
	DeviceMapper.RemapControllerIdToPlatformUserAndDevice(ControllerIndex, InPlatformUser, InDeviceId);
	
	if (ControllerIndex == 0 && (DeviceHand == EControllerHand::Left || DeviceHand == EControllerHand::Right || DeviceHand == EControllerHand::AnyHand))
	{
		return ETrackingStatus::Tracked;
	}
	return ETrackingStatus::NotTracked;
}

// 设置触觉反馈的值
void FPICOXRDPInput::SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues& Values)
{
	
}

// 获取触觉频率范围
void FPICOXRDPInput::GetHapticFrequencyRange(float& MinFrequency, float& MaxFrequency) const
{
	MinFrequency = 0.f;
	MaxFrequency = 1.f;
}

// 获取触觉的震动缩放
float FPICOXRDPInput::GetHapticAmplitudeScale() const
{
	return 1.0f;
}

// 获取控制器电量
int32 FPICOXRDPInput::UPxr_GetControllerPower(int32 Handness)
{
	if (ControllerType == G2)
	{
		return LeftControllerPower;
	}
	else if (ControllerType == Neo2 || ControllerType == Neo3)
	{
		return  Handness == 0 ? LeftControllerPower : RightControllerPower;
	}
	return 0;
}

// 获取控制器链接状态
bool FPICOXRDPInput::UPxr_GetControllerConnectState(int32 Handness)
{
	return  Handness == 0 ? LeftConnectState : RightConnectState;
}

// 获取控制器主输入句柄
bool FPICOXRDPInput::UPxr_GetControllerMainInputHandle(int32& Handness)
{
	if (MainControllerHandle != -1)
	{
		Handness = MainControllerHandle;
		return true;
	}
	return  false;
}
// 
bool FPICOXRDPInput::UPxr_SetControllerMainInputHandle(int32 InHandness)
{
#if PLATFORM_ANDROID
	FPICOXRHMDModule::GetPluginWrapper().SetControllerMainInputHandle(InHandness);
	MainControllerHandle = InHandness;
	return true;
#endif
	return false;
}

void FPICOXRDPInput::SetKeyMapping()
{
#if WITH_EDITOR
	// NOTE：设置按键匹配的 (左键) 
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerX] = FPICOKeyNames::PICOTouch_Left_Thumbstick_X;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerY] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Y;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::Home] = FPICOKeyNames::PICOTouch_Left_Home_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::App] = FPICOKeyNames::PICOTouch_Left_Menu_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::Rocker] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::VolumeUp] = FPICOKeyNames::PICOTouch_Left_VolumeUp_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::VolumeDown] = FPICOKeyNames::PICOTouch_Left_VolumeDown_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::Trigger] = FPICOKeyNames::PICOTouch_Left_Trigger_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::AorX] = FPICOKeyNames::PICOTouch_Left_X_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::BorY] = FPICOKeyNames::PICOTouch_Left_Y_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::Grip] = FPICOKeyNames::PICOTouch_Left_Grip_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerUp] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Up;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerDown] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Down;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerLeft] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Left;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerRight] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Right;

	// NOTE：设置按键匹配的 (右键) 
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerX] = FPICOKeyNames::PICOTouch_Right_Thumbstick_X;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerY] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Y;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::Home] = FPICOKeyNames::PICOTouch_Right_Home_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::App] = FPICOKeyNames::PICOTouch_Right_System_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::Rocker] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::VolumeUp] = FPICOKeyNames::PICOTouch_Right_VolumeUp_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::VolumeDown] = FPICOKeyNames::PICOTouch_Right_VolumeDown_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::Trigger] = FPICOKeyNames::PICOTouch_Right_Trigger_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::AorX] = FPICOKeyNames::PICOTouch_Right_A_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::BorY] = FPICOKeyNames::PICOTouch_Right_B_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::Grip] = FPICOKeyNames::PICOTouch_Right_Grip_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerUp] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Up;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerDown] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Down;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerLeft] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Left;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerRight] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Right;

	// NOTE: 触摸按键左侧
	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::AorX] = FPICOKeyNames::PICOTouch_Left_X_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::BorY] = FPICOKeyNames::PICOTouch_Left_Y_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::Rocker] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::Trigger] = FPICOKeyNames::PICOTouch_Left_Trigger_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::Thumbrest] = FPICOKeyNames::PICOTouch_Left_Thumbrest_Touch;

	// NOTE: 触摸按键右侧
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::AorX] = FPICOKeyNames::PICOTouch_Right_A_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::BorY] = FPICOKeyNames::PICOTouch_Right_B_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::Rocker] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::Trigger] = FPICOKeyNames::PICOTouch_Right_Trigger_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::Thumbrest] = FPICOKeyNames::PICOTouch_Right_Thumbrest_Touch;
#endif
}

void FPICOXRDPInput::ProcessButtonEvent()
{
	const FInputDeviceId DeviceId=IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
	FPlatformUserId PlatformUser = IPlatformInputDeviceMapper::Get().GetUserForInputDevice(DeviceId);
	
	if (LeftConnectState)
	{
		int32 currentStatus = FPICOXRDPManager::GetControllerButtonStatus(EControllerHand::Left);
		int32 LeftControllerEvent[12] = {0};
		LeftControllerEvent[2] = currentStatus & (uint16)EButtonStatus::kHomeClick;
		LeftControllerEvent[3] = currentStatus & (uint16)EButtonStatus::kMenuButtonClick;
		LeftControllerEvent[4] = currentStatus & (uint16)EButtonStatus::kJoystickClick;
		LeftControllerEvent[8] = currentStatus & (uint16)EButtonStatus::kTriggerClick;
		LeftControllerEvent[9] = currentStatus & (uint16)EButtonStatus::kXAClick;
		LeftControllerEvent[10] = currentStatus & (uint16)EButtonStatus::kYBClick;

		for (int32 i = 2; i < EPICOButton::ButtonCount; i++)
		{
			if (LeftControllerEvent[i] != LastLeftControllerButtonState[i] && i != 7 && i != 8 && i < 11)
			{
				LastLeftControllerButtonState[i] = LeftControllerEvent[i];
				if (LeftControllerEvent[i] > 0)
				{
					// 这里用了与控制器通信
					MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][i], PlatformUser,DeviceId, false);
				}
				else if (LeftControllerEvent[i] == 0)
				{
					MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][i], PlatformUser,DeviceId, false);
				}
			}
		}

		//AxisValue
		FPICOXRDPManager::GetControllerAxisValue(EControllerHand::Left, LeftControllerTouchPoint.X, LeftControllerTouchPoint.Y, LeftControllerTriggerValue, LeftControllerGripValue);


		//Trigger Grip Button
		if (LeftControllerTriggerValue > 0.67f && LastLeftControllerButtonState[7] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][7], PlatformUser,DeviceId, false);
		}
		else if (LastLeftControllerButtonState[7] > 0 && LeftControllerTriggerValue <= 0.67f)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][7], PlatformUser,DeviceId, false);
		}
		LastLeftControllerButtonState[7] = LeftControllerTriggerValue > 0.67f ? 1 : 0;

		if (LeftControllerGripValue > 0.67f && LastLeftControllerButtonState[11] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][11], PlatformUser,DeviceId, false);
		}
		else if (LastLeftControllerButtonState[11] > 0 && LeftControllerGripValue <= 0.67f)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][11], PlatformUser,DeviceId, false);
		}
		LastLeftControllerButtonState[11] = LeftControllerGripValue > 0.67f ? 1 : 0;


		//Rocker Up/Down/Left/Right
		if (LeftControllerTouchPoint.Y > 0.7f && LastLeftControllerButtonState[12] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][12], PlatformUser,DeviceId, false);
		}
		else if (LeftControllerTouchPoint.Y <= 0.7f && LastLeftControllerButtonState[12] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][12], PlatformUser,DeviceId, false);
		}
		LastLeftControllerButtonState[12] = LeftControllerTouchPoint.Y > 0.7f ? 1 : 0;

		if (LeftControllerTouchPoint.Y < -0.7f && LastLeftControllerButtonState[13] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][13], PlatformUser,DeviceId, false);
		}
		else if (LeftControllerTouchPoint.Y >= -0.7f && LastLeftControllerButtonState[13] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][13], PlatformUser,DeviceId, false);
		}
		LastLeftControllerButtonState[13] = LeftControllerTouchPoint.Y < -0.7f ? 1 : 0;

		if (LeftControllerTouchPoint.X < -0.7f && LastLeftControllerButtonState[14] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][14], PlatformUser,DeviceId, false);
		}
		else if (LeftControllerTouchPoint.X >= -0.7f && LastLeftControllerButtonState[14] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][14], PlatformUser,DeviceId, false);
		}
		LastLeftControllerButtonState[14] = LeftControllerTouchPoint.X < -0.7f ? 1 : 0;

		if (LeftControllerTouchPoint.X > 0.7f && LastLeftControllerButtonState[15] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][15], PlatformUser,DeviceId, false);
		}
		else if (LeftControllerTouchPoint.X <= 0.7f && LastLeftControllerButtonState[15] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][15], PlatformUser,DeviceId, false);
		}
		LastLeftControllerButtonState[15] = LeftControllerTouchPoint.X > 0.7f ? 1 : 0;

		int TouchArray[5] = {0};
		
		TouchArray[0] = currentStatus & (uint16)ETouchButtonStatus::XATouch;
		TouchArray[1] = currentStatus & (uint16)ETouchButtonStatus::YBTouch;
		TouchArray[2] = currentStatus & (uint16)ETouchButtonStatus::JoystickTouch;
		TouchArray[3] = currentStatus & (uint16)ETouchButtonStatus::TriggerTouch;
		TouchArray[4] = currentStatus & (uint16)ETouchButtonStatus::KBlankTouch;
		
		for (int32 i = 0; i < EPICOTouchButton::ButtonCount; i++)
		{
			// 这里导致信息不发出
			if (TouchArray[i] > 0&&TouchArray[i]!=LastLeftTouchButtonState[i])
			{
				MessageHandler->OnControllerButtonPressed(TouchButtons[EPICOXRControllerHandness::LeftController][i], PlatformUser,DeviceId,  false);
			}
			else if (TouchArray[i] == 0 &&TouchArray[i]!=LastLeftTouchButtonState[i])
			{
				MessageHandler->OnControllerButtonReleased(TouchButtons[EPICOXRControllerHandness::LeftController][i], PlatformUser,DeviceId, false);
			}

			LastLeftTouchButtonState[i]=TouchArray[i];
		}
	}

	
	if (RightConnectState)
	{
		uint16 currentStatus = FPICOXRDPManager::GetControllerButtonStatus(EControllerHand::Right);
		int RightControllerEvent[12] = {0};
		RightControllerEvent[2] = currentStatus & (uint16)EButtonStatus::kHomeClick;
		RightControllerEvent[3] = currentStatus & (uint16)EButtonStatus::kMenuButtonClick;
		RightControllerEvent[4] = currentStatus & (uint16)EButtonStatus::kJoystickClick;
		RightControllerEvent[9] = currentStatus & (uint16)EButtonStatus::kXAClick;
		RightControllerEvent[10] = currentStatus & (uint16)EButtonStatus::kYBClick;

		for (int32 i = 2; i < EPICOButton::ButtonCount; i++)
		{
			if (RightControllerEvent[i] != LastRightControllerButtonState[i] && i != 7 && i != 8 && i < 11)
			{
				LastRightControllerButtonState[i] = RightControllerEvent[i];
				if (RightControllerEvent[i] > 0)
				{
					MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][i], PlatformUser,DeviceId, false);
				}
				else if (RightControllerEvent[i] == 0)
				{
					MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][i], PlatformUser,DeviceId, false);
				}
			}
		}

		FPICOXRDPManager::GetControllerAxisValue(EControllerHand::Right, RightControllerTouchPoint.X, RightControllerTouchPoint.Y, RightControllerTriggerValue, RightControllerGripValue);

		//Trigger Grip Button
		if (RightControllerTriggerValue > 0.67f && LastRightControllerButtonState[7] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][7], PlatformUser,DeviceId, false);
		}
		else if (LastRightControllerButtonState[7] > 0 && RightControllerTriggerValue <= 0.67f)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][7], PlatformUser,DeviceId, false);
		}
		LastRightControllerButtonState[7] = RightControllerTriggerValue > 0.67f ? 1 : 0;

		if (RightControllerGripValue > 0.67f && LastRightControllerButtonState[11] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][11], PlatformUser,DeviceId, false);
		}
		else if (LastRightControllerButtonState[11] > 0 && RightControllerGripValue <= 0.67f)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][11], PlatformUser,DeviceId, false);
		}
		LastRightControllerButtonState[11] = RightControllerGripValue > 0.67f ? 1 : 0;

		//Rocker Up/Down/Left/Right
		if (RightControllerTouchPoint.Y > 0.7f && LastRightControllerButtonState[12] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][12], PlatformUser,DeviceId, false);
		}
		else if (RightControllerTouchPoint.Y <= 0.7f && LastRightControllerButtonState[12] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][12], PlatformUser,DeviceId,false);
		}
		LastRightControllerButtonState[12] = RightControllerTouchPoint.Y > 0.7f ? 1 : 0;

		if (RightControllerTouchPoint.Y < -0.7f && LastRightControllerButtonState[13] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][13], PlatformUser,DeviceId, false);
		}
		else if (RightControllerTouchPoint.Y >= -0.7f && LastRightControllerButtonState[13] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][13], PlatformUser,DeviceId,false);
		}
		LastRightControllerButtonState[13] = RightControllerTouchPoint.Y < -0.7f ? 1 : 0;

		if (RightControllerTouchPoint.X < -0.7f && LastRightControllerButtonState[14] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][14], PlatformUser,DeviceId, false);
		}
		else if (RightControllerTouchPoint.X >= -0.7f && LastRightControllerButtonState[14] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][14], PlatformUser,DeviceId,false);
		}
		LastRightControllerButtonState[14] = RightControllerTouchPoint.X < -0.7f ? 1 : 0;

		if (RightControllerTouchPoint.X > 0.7f && LastRightControllerButtonState[15] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][15], PlatformUser,DeviceId, false);
		}
		else if (RightControllerTouchPoint.X <= 0.7f && LastRightControllerButtonState[15] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][15], PlatformUser,DeviceId,false);
		}
		LastRightControllerButtonState[15] = RightControllerTouchPoint.X > 0.7f ? 1 : 0;
		
		int TouchArray[5] = {0};
		TouchArray[0] = currentStatus & (uint16)ETouchButtonStatus::XATouch;
		TouchArray[1] = currentStatus & (uint16)ETouchButtonStatus::YBTouch;
		TouchArray[2] = currentStatus & (uint16)ETouchButtonStatus::JoystickTouch;
		TouchArray[3] = currentStatus & (uint16)ETouchButtonStatus::TriggerTouch; //这个是对的
		TouchArray[4] = currentStatus & (uint16)ETouchButtonStatus::KBlankTouch;
		
		for (int32 i = 0; i < EPICOTouchButton::ButtonCount; i++)
		{
			if (TouchArray[i] > 1 &&TouchArray[i]!=LastRightTouchButtonState[i])
			{
				MessageHandler->OnControllerButtonPressed(TouchButtons[EPICOXRControllerHandness::RightController][i], PlatformUser,DeviceId, false);
			}
			else if (TouchArray[i] == 0&&TouchArray[i]!=LastRightTouchButtonState[i])
			{
				MessageHandler->OnControllerButtonReleased(TouchButtons[EPICOXRControllerHandness::RightController][i], PlatformUser,DeviceId, false);
			}

			LastRightTouchButtonState[i]=TouchArray[i];
		}
		
	}
}



void FPICOXRDPInput::ProcessButtonAxis()
{
	const FInputDeviceId DeviceId=IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
	FPlatformUserId PlatformUser = IPlatformInputDeviceMapper::Get().GetUserForInputDevice(DeviceId);
#if WITH_EDITOR
	if (LeftConnectState)
	{
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Thumbstick_X, PlatformUser,DeviceId, LeftControllerTouchPoint.X);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Thumbstick_Y, PlatformUser,DeviceId, LeftControllerTouchPoint.Y);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Trigger_Axis, PlatformUser,DeviceId,LeftControllerTriggerValue);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Grip_Axis, PlatformUser,DeviceId, LeftControllerGripValue);
		
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Trigger_Axis_Anime, PlatformUser,DeviceId,LeftControllerTriggerValue);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Grip_Axis_Anime, PlatformUser,DeviceId, LeftControllerGripValue);
	}
	if (RightConnectState)
	{
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Thumbstick_X, PlatformUser,DeviceId, RightControllerTouchPoint.X);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Thumbstick_Y, PlatformUser,DeviceId, RightControllerTouchPoint.Y);

		
		
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Trigger_Axis, PlatformUser,DeviceId, RightControllerTriggerValue);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Grip_Axis, PlatformUser,DeviceId, RightControllerGripValue);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Trigger_Axis_Anime, PlatformUser,DeviceId,RightControllerTriggerValue);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Grip_Axis_Anime, PlatformUser,DeviceId, RightControllerGripValue);
	}
#endif
}

// NOTE: 目前不知道怎么写 
bool FPICOXRDPInput::GetPicoButtonState(FKey InKey)
{
	int32 CurrentStatusL = FPICOXRDPManager::GetControllerButtonStatus(EControllerHand::Left);
	int32 LeftControllerEvent[12] = {0};
	LeftControllerEvent[2] = CurrentStatusL & (uint16)EButtonStatus::kHomeClick;
	LeftControllerEvent[3] = CurrentStatusL & (uint16)EButtonStatus::kMenuButtonClick;
	LeftControllerEvent[4] = CurrentStatusL & (uint16)EButtonStatus::kJoystickClick;
	LeftControllerEvent[8] = CurrentStatusL & (uint16)EButtonStatus::kTriggerClick;
	LeftControllerEvent[9] = CurrentStatusL & (uint16)EButtonStatus::kXAClick;
	LeftControllerEvent[10] = CurrentStatusL & (uint16)EButtonStatus::kYBClick;
	
	uint16 CurrentStatusR = FPICOXRDPManager::GetControllerButtonStatus(EControllerHand::Right);
	int RightControllerEvent[12] = {0};
	RightControllerEvent[2] = CurrentStatusR & (uint16)EButtonStatus::kHomeClick;
	RightControllerEvent[3] = CurrentStatusR & (uint16)EButtonStatus::kMenuButtonClick;
	RightControllerEvent[4] = CurrentStatusR & (uint16)EButtonStatus::kJoystickClick;
	RightControllerEvent[9] = CurrentStatusR & (uint16)EButtonStatus::kXAClick;
	RightControllerEvent[10] = CurrentStatusR & (uint16)EButtonStatus::kYBClick;

	return false;
}

void FPICOXRDPInput::ProcessEnhancedInput()
{
	if (LeftConnectState)
	{
		uint16 currentStatus = FPICOXRDPManager::GetControllerButtonStatus(EControllerHand::Right);
		int RightControllerEvent[12] = {0};
		RightControllerEvent[2] = currentStatus & (uint16)EButtonStatus::kHomeClick;
		RightControllerEvent[3] = currentStatus & (uint16)EButtonStatus::kMenuButtonClick;
		RightControllerEvent[4] = currentStatus & (uint16)EButtonStatus::kJoystickClick;
		RightControllerEvent[9] = currentStatus & (uint16)EButtonStatus::kXAClick;
		RightControllerEvent[10] = currentStatus & (uint16)EButtonStatus::kYBClick;
		for (const auto& MappingContext : InputMappingContextToPriorityMap)
		{
			for (const FEnhancedActionKeyMapping& Mapping : MappingContext.Key->GetMappings())
			{
				const UInputAction* InputAction= Mapping.Action;
				FInputActionValue InputValue;
				if (!Mapping.Action)
				{
					continue;
				}
				
				switch (InputAction->ValueType)
				{
				case EInputActionValueType::Boolean:
					{
						// 这样输入的值就是指定的
						InputValue = FInputActionValue(true); //GetPicoButtonValue
					}
					break;
				case EInputActionValueType::Axis1D:
					{
						InputValue = FInputActionValue(0.5f);
					}
					break;
				default:
					break;
				}
				TArray<TObjectPtr<UInputTrigger>> Triggers = InputAction->Triggers;
				TArray<TObjectPtr<UInputModifier>> Modifiers = InputAction->Modifiers;
				// Lambda 表达式用于给子系统插入数据，然后用于绑定调用
				auto InjectSubsystemInput = [InputAction, InputValue, Triggers, Modifiers](IEnhancedInputSubsystemInterface* Subsystem)
				{
					if (Subsystem)
					{
						// 为操作注入操作
						Subsystem->InjectInputForAction(InputAction, InputValue, Modifiers, Triggers);
					}
				};
				// 增强输入模块遍历子系统，调用函数
				IEnhancedInputModule::Get().GetLibrary()->ForEachSubsystem(InjectSubsystemInput);
			}
		}
	}
	
	
}

//  添加不存在的键值
void FPICOXRDPInput::AddNonExistingKey(const TArray<FKey>& ExistAllKeys, const FKeyDetails& KeyDetails)
{
	if (!ExistAllKeys.Contains(KeyDetails.GetKey()))
	{
		EKeys::AddKey(KeyDetails);
	}
}

// 这个没什么问题
void FPICOXRDPInput::SetHeadPosition(FVector Position)
{
	SourcePosition = Position;
}

// 设置头戴的旋转
void FPICOXRDPInput::SetHeadOrientation(FQuat Orientation)
{
	SourceOrientation = Orientation;
}

#define FloatAxis Axis1D
void FPICOXRDPInput::RegisterKeys()
{
#if WITH_EDITOR
	const FName NAME_PICOTouchController(TEXT("PICOTouch"));
	if (EKeys::GetMenuCategoryDisplayName(NAME_PICOTouchController).ToString()!="PICO Touch")
	{
		EKeys::AddMenuCategoryDisplayInfo(NAME_PICOTouchController, LOCTEXT("PICOTouchSubCategory", "PICO Touch"), TEXT("GraphEditor.PadEvent_16x"));
	}

	const FName NAME_PICOHandController(TEXT("PICOHand"));
	if (EKeys::GetMenuCategoryDisplayName(NAME_PICOHandController).ToString()!="PICO Hand")
	{
		EKeys::AddMenuCategoryDisplayInfo(NAME_PICOHandController, LOCTEXT("PICOHandSubCategory", "PICO Hand"), TEXT("GraphEditor.PadEvent_16x"));
	}
	
	TArray<FKey> ExistAllKeys;
	EKeys::GetAllKeys(ExistAllKeys);
	
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_X_Click, LOCTEXT("PICOTouch_Left_X_Click", "PICO Touch (L) X Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Y_Click, LOCTEXT("PICOTouch_Left_Y_Click", "PICO Touch (L) Y Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_X_Touch, LOCTEXT("PICOTouch_Left_X_Touch", "PICO Touch (L) X Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Y_Touch, LOCTEXT("PICOTouch_Left_Y_Touch", "PICO Touch (L) Y Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Menu_Click, LOCTEXT("PICOTouch_Left_Menu_Click", "PICO Touch (L) Menu"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Grip_Click, LOCTEXT("PICOTouch_Left_Grip_Click", "PICO Touch (L) Grip"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Grip_Axis, LOCTEXT("PICOTouch_Left_Grip_Axis", "PICO Touch (L) Grip Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Trigger_Click, LOCTEXT("PICOTouch_Left_Trigger_Click", "PICO Touch (L) Trigger"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Trigger_Axis, LOCTEXT("PICOTouch_Left_Trigger_Axis", "PICO Touch (L) Trigger Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Trigger_Touch, LOCTEXT("PICOTouch_Left_Trigger_Touch", "PICO Touch (L) Trigger Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_X, LOCTEXT("PICOTouch_Left_Thumbstick_X", "PICO Touch (L) Thumbstick X"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Y, LOCTEXT("PICOTouch_Left_Thumbstick_Y", "PICO Touch (L) Thumbstick Y"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Click, LOCTEXT("PICOTouch_Left_Thumbstick_Click", "PICO Touch (L) Thumbstick"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Touch, LOCTEXT("PICOTouch_Left_Thumbstick_Touch", "PICO Touch (L) Thumbstick Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Home_Click, LOCTEXT("PICOTouch_Left_Home_Click", "PICO Touch (L) Home Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_VolumeUp_Click, LOCTEXT("PICOTouch_Left_VolumeUp_Click", "PICO Touch (L) Volume Up Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_VolumeDown_Click, LOCTEXT("PICOTouch_Left_VolumeDown_Click", "PICO Touch (L) Volume Down Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbrest_Touch, LOCTEXT("PICOTouch_Left_Thumbrest_Touch", "PICO Touch (L) Thumbrest Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Up, LOCTEXT("PICOTouch_Left_Thumbstick_Up", "PICO Touch (L) Thumbstick Up"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Down, LOCTEXT("PICOTouch_Left_Thumbstick_Down", "PICO Touch (L) Thumbstick Down"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Left, LOCTEXT("PICOTouch_Left_Thumbstick_Left", "PICO Touch (L) Thumbstick Left"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Right, LOCTEXT("PICOTouch_Left_Thumbstick_Right", "PICO Touch (L) Thumbstick Right"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));

	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Grip_Axis_Anime, LOCTEXT("PICOTouch_Left_Grip_Axis_Anime", "PICO Touch (L) Grip Axis Anime"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Trigger_Axis_Anime, LOCTEXT("PICOTouch_Left_Trigger_Axis_Anime", "PICO Touch (L) Trigger Axis Anime"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_A_Click, LOCTEXT("PICOTouch_Right_A_Click", "PICO Touch (R) A Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_B_Click, LOCTEXT("PICOTouch_Right_B_Click", "PICO Touch (R) B Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_A_Touch, LOCTEXT("PICOTouch_Right_A_Touch", "PICO Touch (R) A Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_B_Touch, LOCTEXT("PICOTouch_Right_B_Touch", "PICO Touch (R) B Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_System_Click, LOCTEXT("PICOTouch_Right_System_Click", "PICO Touch (R) System"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Grip_Click, LOCTEXT("PICOTouch_Right_Grip_Click", "PICO Touch (R) Grip"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Grip_Axis, LOCTEXT("PICOTouch_Right_Grip_Axis", "PICO Touch (R) Grip Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Trigger_Click, LOCTEXT("PICOTouch_Right_Trigger_Click", "PICO Touch (R) Trigger"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Trigger_Axis, LOCTEXT("PICOTouch_Right_Trigger_Axis", "PICO Touch (R) Trigger Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Trigger_Touch, LOCTEXT("PICOTouch_Right_Trigger_Touch", "PICO Touch (R) Trigger Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_X, LOCTEXT("PICOTouch_Right_Thumbstick_X", "PICO Touch (R) Thumbstick X"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Y, LOCTEXT("PICOTouch_Right_Thumbstick_Y", "PICO Touch (R) Thumbstick Y"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Click, LOCTEXT("PICOTouch_Right_Thumbstick_Click", "PICO Touch (R) Thumbstick"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Touch, LOCTEXT("PICOTouch_Right_Thumbstick_Touch", "PICO Touch (R) Thumbstick Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Home_Click, LOCTEXT("PICOTouch_Right_Home_Click", "PICO Touch (R) Home Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_VolumeUp_Click, LOCTEXT("PICOTouch_Right_VolumeUp_Click", "PICO Touch (R) Volume Up Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_VolumeDown_Click, LOCTEXT("PICOTouch_Right_VolumeDown_Click", "PICO Touch (R) Volume Down Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbrest_Touch, LOCTEXT("PICOTouch_Right_Thumbrest_Touch", "PICO Touch (R) Thumbrest Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Up, LOCTEXT("PICOTouch_Right_Thumbstick_Up", "PICO Touch (R) Thumbstick Up"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Down, LOCTEXT("PICOTouch_Right_Thumbstick_Down", "PICO Touch (R) Thumbstick Down"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Left, LOCTEXT("PICOTouch_Right_Thumbstick_Left", "PICO Touch (R) Thumbstick Left"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Right, LOCTEXT("PICOTouch_Right_Thumbstick_Right", "PICO Touch (R) Thumbstick Right"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));

	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Grip_Axis_Anime, LOCTEXT("PICOTouch_Right_Grip_Axis_Anime", "PICO Touch (R) Grip Axis Anime"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Trigger_Axis_Anime, LOCTEXT("PICOTouch_Right_Trigger_Axis_Anime", "PICO Touch (R) Trigger Axis Anime"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));

	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_Pinch, LOCTEXT("PICOHand_Left_Pinch", "PICO Hand (L) Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_Pinch, LOCTEXT("PICOHand_Right_Pinch", "PICO Hand (R) Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_PinchStrength, LOCTEXT("PICOHand_Left_PinchStrength", "PICO Hand (L) Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_PinchStrength, LOCTEXT("PICOHand_Right_PinchStrength", "PICO Hand (R) Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
#endif
	
}
#undef FloatAxis

void FPICOXRDPInput::BuildActions()
{
	
	const UEnhancedInputDeveloperSettings* InputSettings = GetDefault<UEnhancedInputDeveloperSettings>();
	if (InputSettings)
	{
		for (const auto& Context : InputSettings->DefaultMappingContexts)
		{
			if (Context.InputMappingContext)
			{
				TStrongObjectPtr<const UInputMappingContext> Obj(Context.InputMappingContext.LoadSynchronous());
				InputMappingContextToPriorityMap.Add(Obj, Context.Priority);
			}
			else
			{
				UE_LOG(LogHMD, Warning, TEXT("Default Mapping Contexts contains an Input Mapping Context set to \"None\", ignoring while building OpenXR actions."));
			}
		}
	}
	
}


#undef LOCTEXT_NAMESPACE
