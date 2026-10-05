"""Chinese presentation for decoded frames. Raw identifiers remain in export records."""
FAULT_TEXT={'NONE':'无故障','MON_OFFLINE':'电流监测持续失联','VIN_UV':'输入欠压',
            'VOUT_OV':'输出过压','IIN_OC':'输入过流','IOUT_OC':'输出过流','OTP':'过温'}
COMMAND_TEXT={'INFO':'查询设备','STATUS':'查询状态','SET_V':'设定电压','SET_I':'设定关断电流',
              'OUT':'输出开关','CLEAR':'清除故障','CAL_GET':'读取测量校准','CAL_RAW':'读取原始测量',
              'CAL_SET_V':'设置电压测量系数','CAL_SET_I':'设置电流测量系数','CAL_SAVE':'保存校准',
              'CAL_RESET':'重置校准','CAL_POINT_GET':'读取调压校准点','CAL_POINT_START':'启动调压校准',
              'CAL_POINT_STEP':'微调DAC','CAL_POINT_CAPTURE':'采集并保存','CAL_POINT_STOP':'停止校准',
              'CAL_POINT_STATUS':'查询校准状态','LOG':'回传开关','LOG_RATE':'回传周期','PING':'连接测试',
              'FW_STATUS':'查询升级','FW_BEGIN':'准备固件','FW_DATA':'上传固件','FW_END':'校验固件',
              'FW_APPLY':'应用固件','FW_ABORT':'取消升级','REBOOT':'重启','HELP':'查询命令','DIAG':'诊断'}
DETAIL_TEXT={'ON':'开启','OFF':'关闭','PONG':'连接正常','OUTPUT_OFF':'输出已关闭',
             'FAULT_CLEARED_OUTPUT_OFF':'故障已清除，输出保持关闭','DIRECT_DAC':'已开启直接DAC校准',
             'DAC_APPLIED':'DAC已写入','SAVED_OUTPUT_OFF':'已保存并关闭输出','RAM_UPDATED':'参数已更新',
             'W25Q256_OK':'闪存保存成功','READY':'已就绪','VERIFIED':'校验成功','REBOOTING':'正在重启',
             'CANCELLED':'已取消','RAM_DEFAULTS':'已恢复默认参数'}
ERROR_TEXT={'FAULT_ACTIVE_USE_CLEAR':'故障仍锁存，请先清除故障','OUTPUT_MUST_BE_OFF':'请先关闭输出',
            'FLASH_BUSY_RETRY':'闪存忙，请稍后重试','SETTINGS_BUSY_RETRY':'设置正在保存，请稍后重试',
            'OWNED_BY_OTHER_PORT':'另一通信端口正在校准','ACTIVE_STOP_FIRST':'请先停止调压校准',
            'INVALID_COMMAND_PAYLOAD':'命令数据不完整或范围错误','SEQUENCE_CONFLICT':'重复序号对应不同命令',
            'RANGE':'参数超出范围','REJECTED':'参数未被接受','DAC_MODE':'当前使用直接DAC调压',
            'INDEX_0_15':'校准点序号应为0～15','INACTIVE_OR_INVALID_METER_VALUE':'未进入校准或实测值无效',
            'INACTIVE_OR_CODE_RANGE_OR_DAC_WRITE':'未进入校准、DAC码超限或DAC写入失败',
            'SESSION_ACTIVE':'已有升级会话','FLASH_OR_RANGE':'闪存操作失败或固件范围错误',
            'FW_UPDATE_ACTIVE':'正在升级，请等待完成','W25Q256_WRITE_FAILED':'闪存保存失败'}

def number(v,k,scale=1):
    try:return int(v.get(k,'0'))/scale
    except ValueError:return 0

def format_frame(frame):
    v=frame.values;k=frame.kind
    if k=='telemetry':
        return (f"状态 | 输出{'开启' if v.get('out')=='1' else '关闭'} | "
                f"{number(v,'vout',1000):.3f} V  {number(v,'iout',1e6):.4f} A  "
                f"{number(v,'pout',1000):.2f} W | 输入 {number(v,'vin',1000):.3f} V | "
                f"低频波动 {v.get('vpp','—')} mV | {FAULT_TEXT.get(v.get('fault'), '未知故障')} | "
                f"采样 {v.get('hz','—')} Hz / 最大间隔 {v.get('maxgap','—')} ms"+
                (f" | PI{'微调' if v.get('pid')=='1' else '暂停'} / 误差 {v.get('pe','—')} mV" if v.get('trim_available')=='1' else ''))
    if k=='info':return f"设备 | 固件 {v.get('fw')} | 硬件 V2 | 调压 {number(v,'vmin',1000):g}～{number(v,'vmax',1000):g} V | 二进制协议 {v.get('proto')}"
    if k in ('ack','error'):
        name=COMMAND_TEXT.get(v.get('cmd'),'设备命令')
        value=v.get('detail' if k=='ack' else 'reason','')
        detail=(DETAIL_TEXT if k=='ack' else ERROR_TEXT).get(value)
        if detail is None:
            detail=value if value.replace('/','').replace('-','').isdigit() else ('操作完成' if k=='ack' else '操作失败，诊断码：'+value)
        return f"{'确认' if k=='ack' else '错误'} | {name} | {detail}"
    if k=='fault':return (f"保护关断 | {FAULT_TEXT.get(v.get('reason'),'未知故障')} | "
                            f"电压 {number(v,'vout',1000):.3f} V | 原始输出电流 {number(v,'iout_uncal',1e6):.4f} A | "
                            f"设定关断电流 {number(v,'ilim',1000):.3f} A | DAC {v.get('dac')}")
    if k=='curve':return f"校准表 | 共 {v.get('total')} 点 | {'已保存' if v.get('saved')=='1' else '尚未保存'}"
    if k=='curve_point':return f"校准点 | {number(v,'target',1000):g} V | "+(f"实测 {number(v,'actual',1000):.3f} V / DAC {v.get('dac')}" if v.get('valid')=='1' else '待校准')
    if k=='curve_state':return f"校准状态 | {'运行' if v.get('active')=='1' else '停止'} | {number(v,'target',1000):g} V | DAC {v.get('dac')} | 实测 {number(v,'vout',1000):.3f} V"
    if k=='calibration':return f"测量校准 | 电压增益 {v.get('vgain')} / 偏移 {v.get('voff')} mV | 原始电压 {v.get('vraw')} mV"
    return '诊断 | '+frame.raw

def format_command(command):
    from wire_protocol import OPS
    name=next((k for k in sorted(OPS,key=len,reverse=True) if command==k or command.startswith(k+' ')),None)
    return COMMAND_TEXT.get((name or '').replace(' ','_'),'设备命令')+' '+(command[len(name):].strip() if name else command)
