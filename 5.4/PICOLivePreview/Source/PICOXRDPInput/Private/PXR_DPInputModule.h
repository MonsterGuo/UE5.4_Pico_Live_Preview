//Unreal® Engine, Copyright 1998 – 2023, Epic Games, Inc. All rights reserved.

#pragma once
#include "IPXR_DPInputModule.h"
#include "InputDevice.h"
#include "Templates/SharedPointer.h"

class FPICOXRDPInputModule : public IPICOXRDPInputModule
{
public:
	FPICOXRDPInputModule();
	virtual ~FPICOXRDPInputModule();
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// NOTE: 这里相当于是定义了一个输入设备
	virtual TSharedPtr< class IInputDevice > CreateInputDevice(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) override;
private:
	TSharedPtr<class FPICOXRDPInput> InputDevice;
};

