import { SystemInfo as ISystemInfo, SystemASIC as ISystemASIC } from '../app/generated';

export interface DevMockData {
  systemInfo: Partial<ISystemInfo> & Record<string, any>;
  asicSettings: Partial<ISystemASIC> & Record<string, any>;
  poolWorkerName: string;
  poolHashrate: string;
  poolUseragent: string;
  statistics: {
    hashrate: number[];
    power: number[];
  };
}
