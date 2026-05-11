import { SystemInfo as ISystemInfo, SystemASIC as ISystemASIC } from '../app/generated';

export interface DevMockData {
  // ISystemInfo (not Partial) so TypeScript errors when required fields drift out of sync with the API
  systemInfo: ISystemInfo & Record<string, any>;
  asicSettings: Partial<ISystemASIC> & Record<string, any>;
  poolWorkerName: string;
  poolHashrate: string;
  poolUseragent: string;
  statistics: {
    hashrate: number[];
    power: number[];
  };
}
