# GUI の代わりのテスト用 RTC：シーケンス指令を送り、シーケンス状態を表示する
# 使い方は README の「偽の Task_provider でつないで試す」を参照
import socket
import subprocess
import sys
import time

import OpenRTM_aist
import RTC

H = "/localhost:2809/" + socket.gethostname() + ".host_cxt"
SPEC = ["implementation_id", "gui_stub", "type_name", "gui_stub", "description", "test",
        "version", "1.0", "vendor", "test", "category", "test", "activity_type", "PERIODIC",
        "kind", "DataFlowComponent", "max_instance", "1", "language", "Python",
        "lang_type", "SCRIPT", ""]
SEQ = {0: "なし", 1: "実行中", 2: "中断中"}
CMD = ["SELF_DIAG", "LOAD_MAP", "MECHA_HOME", "START_SEQUENCE", "PAUSE", "RESUME", "CANCEL",
       "RECEIVE", "MOVE", "EXCHANGE", "SEND_OUT", "RETREAT", "CLEAR_ERROR", "RESET_POSE",
       "ESTOP_ENABLE", "ESTOP_DISENABLE"]


class GuiStub(OpenRTM_aist.DataFlowComponentBase):
    def __init__(self, mgr):
        super().__init__(mgr)
        self.seq_cmd = RTC.TimedLongSeq(RTC.Time(0, 0), [])
        self.seq_cmd_out = OpenRTM_aist.OutPort("seq_cmd", self.seq_cmd)
        self.tp_cmd = RTC.TimedLongSeq(RTC.Time(0, 0), [])
        self.tp_cmd_out = OpenRTM_aist.OutPort("tp_cmd", self.tp_cmd)
        self.state = RTC.TimedLongSeq(RTC.Time(0, 0), [])
        self.state_in = OpenRTM_aist.InPort("seq_state", self.state)

    def onInitialize(self):
        self.addOutPort("seq_cmd", self.seq_cmd_out)
        self.addOutPort("tp_cmd", self.tp_cmd_out)
        self.addInPort("seq_state", self.state_in)
        return RTC.RTC_OK


def cmdname(c):
    return CMD[c] if 0 <= c < len(CMD) else str(c)


def main():
    mgr = OpenRTM_aist.Manager.init([sys.argv[0], "-o", "corba.nameservers:localhost:2809",
                                     "-o", "naming.formats:%h.host_cxt/%n.rtc",
                                     "-o", "logger.enable:NO"])
    mgr.activateManager()
    mgr.registerFactory(OpenRTM_aist.Properties(defaults_str=SPEC), GuiStub, OpenRTM_aist.Delete)
    comp = mgr.createComponent("gui_stub")
    mgr.runManager(True)
    time.sleep(1)

    M = H + "/bot_sequence_manager0.rtc"
    F = H + "/fake_task_provider0.rtc"
    G = H + "/gui_stub0.rtc"
    for a, b in [(G + ":seq_cmd", M + ":seq_cmd"), (M + ":seq_state", G + ":seq_state"),
                 (G + ":tp_cmd", F + ":cmd")]:
        subprocess.run(["rtcon", a, b], check=True, shell=True)

    t0 = time.time()
    last = None

    def poll(sec):
        nonlocal last
        end = time.time() + sec
        while time.time() < end:
            while comp.state_in.isNew():
                d = list(comp.state_in.read().data)
                key = tuple(d)
                if key != last:
                    last = key
                    print("%5.1fs  Bot%d %s  step %d/%d %s  停止原因 %d  再開可 %d  | 直近の指令 %s → %s %s" % (
                        time.time() - t0, d[0], SEQ.get(d[1], d[1]), d[3], d[4],
                        cmdname(d[5]) if d[5] >= 0 else "-", d[6], d[7],
                        cmdname(d[8]) if d[8] >= 0 else "-",
                        {1: "受付", 3: "拒否"}.get(d[9], "-"),
                        ("理由 %d" % d[10]) if d[10] else ""), flush=True)
            time.sleep(0.02)

    def send(port_data, port, arr, label):
        port_data.data = arr
        port.write()
        print("%5.1fs  >>> %s %s" % (time.time() - t0, label, arr), flush=True)

    poll(1)
    send(comp.seq_cmd, comp.seq_cmd_out, [1, 3, 2, 0, 0, 0], "GUI→CSV管理 START_SEQUENCE(NORMAL)")
    poll(3)
    send(comp.seq_cmd, comp.seq_cmd_out, [1, 4, 0, 0, 0, 0], "GUI→CSV管理 PAUSE")
    send(comp.tp_cmd, comp.tp_cmd_out, [1, 4, 0, 0, 0, 0], "GUI→Task_provider PAUSE")
    poll(1)
    send(comp.seq_cmd, comp.seq_cmd_out, [1, 3, 2, 0, 0, 0], "GUI→CSV管理 START_SEQUENCE（中断中なので拒否されるはず）")
    poll(1)
    send(comp.seq_cmd, comp.seq_cmd_out, [1, 5, 0, 0, 0, 0], "GUI→CSV管理 RESUME")
    poll(10)
    mgr.shutdown()


if __name__ == "__main__":
    main()
