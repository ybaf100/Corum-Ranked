import { IsArray, ValidateNested, IsIn, IsInt, IsNumber, IsObject, IsOptional, IsString, IsUUID, Length, Max, Min, ValidateIf } from "class-validator";
import type { SurvivalDifficultyGroup, SurvivalVerifiedMap } from "@corum-ranked/rules";
import { Type } from "class-transformer";
import { EnvironmentRecheckDto, InstalledModDto } from "../session/session.dto.js";

export class CreateSurvivalDebugDto extends EnvironmentRecheckDto {
  @IsString() @Length(1,128) public password!:string;
  @IsUUID() public requestId!:string;
  @IsIn(["RED_BRONZE","SILVER","GOLD"]) public averageGroup!:SurvivalDifficultyGroup;
  @IsOptional() @IsInt() @Min(1) @Max(16) public forceRank?:number;
  @IsOptional() @IsInt() @Min(1) @Max(15) public botCount?:number;
}
export class SurvivalDebugEventDto {
  @IsUUID() public eventId!:string;
  @IsIn(["ASSETS_READY","PREPARATION_FAILED","ENTERED","ATTEMPT_START","PROGRESS","ATTEMPT_END","FORFEIT","PRESENTATION_COMPLETE"])
  public type!:"ASSETS_READY"|"PREPARATION_FAILED"|"ENTERED"|"ATTEMPT_START"|"PROGRESS"|"ATTEMPT_END"|"FORFEIT"|"PRESENTATION_COMPLETE";
  @IsString() @Length(1,80) public roundKey!:string;
  @IsInt() @Min(1) public mapGeneration!:number;
  @ValidateIf((event:SurvivalDebugEventDto)=>!["FORFEIT","PRESENTATION_COMPLETE"].includes(event.type)) @IsInt() @Min(40000000) public levelId!:number;
  @ValidateIf((event:SurvivalDebugEventDto)=>["ASSETS_READY","ENTERED","ATTEMPT_START"].includes(event.type))
  @IsArray() @ValidateNested({each:true}) @Type(()=>InstalledModDto) public installedMods?:InstalledModDto[];
  @IsOptional() @IsUUID() public attemptId?:string;
  @IsOptional() @IsNumber({allowInfinity:false,allowNaN:false}) @Min(0) @Max(100) public progress?:number;
  @IsOptional() @IsObject() public certificate?:SurvivalVerifiedMap;
  @IsOptional() @IsString() @Length(1,160) public reason?:string;
}
export class ForceSurvivalDebugDto {
  @IsString() @Length(1,128) public password!:string;
  @IsInt() @Min(1) @Max(16) public rank!:number;
}
