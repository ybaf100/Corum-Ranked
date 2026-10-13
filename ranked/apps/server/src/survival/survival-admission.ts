import { ServiceUnavailableException } from "@nestjs/common";
import type { SqlExecutor } from "../database/database.port.js";
import type { SurvivalConfigDocument } from "../config/survival-config.service.js";

/** Lock before account/queue rows, in refresh, public admission and Debug. */
export async function lockSurvivalAdmission(tx:SqlExecutor):Promise<void> {
  await tx.query("SELECT pg_advisory_xact_lock(hashtext('survival-admission'))");
}
export async function assertSurvivalAdmission(tx:SqlExecutor,captured:SurvivalConfigDocument):Promise<void> {
  const row=(await tx.query<{enabled:boolean;generation:string}>("SELECT enabled,generation FROM sur_mode_state WHERE singleton=TRUE")).rows[0];
  if(!captured.config.enabled || row?.enabled===false)
    throw new ServiceUnavailableException({code:"SURVIVAL_DISABLED",message:"Survival is disabled"});
  if(row && row.generation!==captured.generation)
    throw new ServiceUnavailableException({code:"SURVIVAL_CONFIG_CHANGED",message:"Survival configuration is refreshing; retry admission"});
}
