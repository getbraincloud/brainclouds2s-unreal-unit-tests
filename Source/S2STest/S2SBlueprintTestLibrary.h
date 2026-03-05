// Copyright 2026 bitHeads, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "S2SBlueprintTestLibrary.generated.h"

USTRUCT(BlueprintType)
struct FS2STestCredentials
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString AppId;
	UPROPERTY(BlueprintReadOnly) FString ServerName;
	UPROPERTY(BlueprintReadOnly) FString ServerSecret;
	UPROPERTY(BlueprintReadOnly) FString Url;
};

/**
 * Blueprint-callable helpers for S2S automation tests.
 * Reads credentials from Config/BrainCloudSettings.ini so Blueprint tests
 * use the same source of truth as the C++ automation tests.
 */
UCLASS()
class S2STEST_API US2SBlueprintTestLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Reads S2S credentials from Config/BrainCloudSettings.ini. */
	UFUNCTION(BlueprintCallable, Category = "S2S|Testing")
	static FS2STestCredentials LoadS2STestCredentials();

	/**
	 * Parses an S2S JSON response, logs the outcome, and returns whether it succeeded.
	 *
	 * Checks the "status" field: 200 = success, anything else = failure.
	 * Call this from a Blueprint test after each API callback, then branch on the
	 * return value to call Finish Test (Succeeded) or Finish Test (Failed).
	 *
	 * @param JsonResponse  The raw JSON string received from the S2S callback.
	 * @param Context       Optional label shown in the log (e.g. "Authenticate") to
	 *                      identify which call produced this response.
	 * @return True if status == 200, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "S2S|Testing")
	static bool ReportResult(const FString& JsonResponse, const FString& Context);

	UFUNCTION(BlueprintCallable, Category = "S2S|Testing")
	static bool ReportResultSimple(bool success, const FString& message, const FString& Context);

};