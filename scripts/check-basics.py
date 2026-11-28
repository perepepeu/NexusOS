"""Audita video, respostas Limine, timer/idle, teclado e ciclos de energia.
--soak-seconds 3600 executa uma hora REAL, sem usar aceleracao de relogio.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import os
import re
import subprocess
import tempfile
import time

spec=importlib.util.spec_from_file_location("power_checks", "scripts/check-power.py")
power=importlib.util.module_from_spec(spec); spec.loader.exec_module(power)
Monitor=power.Monitor
READY=b"Terminal pronto. Digite help."
parser=argparse.ArgumentParser()
parser.add_argument("--soak-seconds",type=int,default=0)
parser.add_argument("--mode",choices=("all","bios","uefi"),default="all")
parser.add_argument("--case",help="Executa apenas o caso com este nome (ex.: controls).")
args=parser.parse_args()
if args.soak_seconds < 0: parser.error("duracao negativa")
results=[]

def clean(log): return re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]",b"",log.read_bytes())
def cpu_seconds(pid):
    fields=Path(f"/proc/{pid}/stat").read_text().rsplit(")",1)[1].split()
    return (int(fields[11])+int(fields[12]))/os.sysconf("SC_CLK_TCK")

def run(mode,label,option="",resolution=None,fatal=None,controls=False,soak=0):
    if args.case and label!=args.case: return
    config=Path(f"build/limine-basics-{label}-{mode}.conf")
    content=Path("boot/limine.conf").read_text()
    if option: content+=f"\n    cmdline: selftest={option}\n"
    if resolution: content+=f"\n    resolution: {resolution[0]}x{resolution[1]}x32\n"
    config.write_text(content)
    image=Path(f"build/nexusos-basics-{label}-{mode}.iso")
    built=subprocess.run(["bash","scripts/build-iso.sh",str(config),str(image)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if built.returncode: raise RuntimeError(built.stdout.decode(errors="replace"))
    log=Path(f"build/basics-{label}-{mode}.log")
    result={"case":label,"mode":mode,"passed":False}
    with tempfile.TemporaryDirectory(prefix="nexus-basics-") as directory:
        sock=Path(directory)/"qmp.sock"
        command=["qemu-system-x86_64","-machine","q35","-accel","tcg","-m","256M","-cdrom",str(image),
                 "-boot","d","-display","none","-serial","stdio","-monitor","none","-net","none","-no-reboot",
                 "-qmp",f"unix:{sock},server=on,wait=off"]
        if controls: command.remove("-no-reboot")
        if mode=="uefi": command += ["-drive","if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd"]
        with log.open("wb") as output,subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT) as process:
            monitor=None
            try:
                def wait(predicate,label,timeout=35):
                    end=time.monotonic()+timeout
                    while not predicate():
                        if process.poll() is not None or time.monotonic()>end: raise RuntimeError(f"{label}: {log}")
                        if monitor: monitor.receive(0.03)
                        else: time.sleep(0.03)
                wait(lambda:sock.exists(),"QMP")
                monitor=Monitor(str(sock))
                if fatal:
                    wait(lambda:b"Sistema interrompido. Reinicie a maquina." in log.read_bytes(),"panic previsto")
                    assert f"KERNEL PANIC codigo={fatal}:".encode() in log.read_bytes()
                    assert READY not in log.read_bytes()
                    assert monitor.execute("query-status")["running"]
                else:
                    wait(lambda:READY in log.read_bytes(),"terminal")
                    assert b"Globais/BSS iniciais: OK" in log.read_bytes()
                    monitor.events.clear()
                    monitor.type_command("echo basics-ok")
                    wait(lambda:b"\r\nbasics-ok\r\n" in log.read_bytes(),"echo")
                    if resolution:
                        expected=f"Framebuffer: {resolution[0]}x{resolution[1]} ".encode()
                        assert expected in log.read_bytes(),f"Resolucao nao obtida: {log}"
                        screenshot=Path(f"build/basics-{label}-{mode}.ppm")
                        monitor.execute("screendump",{"filename":str(screenshot.resolve())})
                        header,dimensions,maximum,pixels=screenshot.read_bytes().split(b"\n",3)
                        assert header==b"P6" and tuple(map(int,dimensions.split()))==resolution and maximum==b"255"
                        assert len(pixels)==resolution[0]*resolution[1]*3
                    if option=="boot-no-framebuffer": assert b"usando serial" in log.read_bytes()
                    if option=="boot-no-modules": assert b"Nenhum modulo Limine disponivel" in log.read_bytes()
                    if controls or soak:
                        def uptime():
                            previous=len(re.findall(rb"Uptime: (\d+)\.(\d\d)s",clean(log)))
                            monitor.type_command("uptime")
                            wait(lambda:len(re.findall(rb"Uptime: (\d+)\.(\d\d)s",clean(log)))>previous,"uptime")
                            a,b=re.findall(rb"Uptime: (\d+)\.(\d\d)s",clean(log))[-1]
                            return int(a)+int(b)/100
                        guest_start=uptime(); wall_start=time.monotonic(); cpu_start=cpu_seconds(process.pid)
                        time.sleep(5)
                        idle_cpu=(cpu_seconds(process.pid)-cpu_start)/(time.monotonic()-wall_start)
                        guest_end=uptime(); elapsed=time.monotonic()-wall_start
                        error=abs((guest_end-guest_start)-elapsed)/elapsed
                        assert idle_cpu<0.35,f"CPU idle excessiva: {idle_cpu:.2%}"
                        assert error<0.20,f"Timer fora da tolerancia de 20%: {error:.2%}"
                        result.update(idle_cpu_fraction=idle_cpu,timer_error_fraction=error,timer_elapsed_seconds=elapsed)
                    if controls:
                        # Burst sem delay entre eventos, maior que um comando do terminal.
                        monitor.execute("input-send-event",{"events":[
                            {"type":"key","data":{"down":down,"key":{"type":"qcode","data":"x"}}}
                            for _ in range(256) for down in (True,False)]})
                        time.sleep(0.5)
                        monitor.execute("input-send-event",{"events":[
                            {"type":"key","data":{"down":down,"key":{"type":"qcode","data":key}}}
                            for key,down in (("ctrl",True),("u",True),("u",False),("ctrl",False))]})
                        monitor.type_command("echo burst-ok"); wait(lambda:b"\r\nburst-ok\r\n" in log.read_bytes(),"recuperacao do burst")
                        for i in range(12): monitor.type_command(f"echo h{i}")
                        previous=clean(log).count(b"\r\nh4\r\n")
                        for _ in range(12):
                            monitor.execute("input-send-event",{"events":[{"type":"key","data":{"down":d,"key":{"type":"qcode","data":"up"}}} for d in (True,False)]})
                            time.sleep(0.03)
                        monitor.type_command("")
                        wait(lambda:clean(log).count(b"\r\nh4\r\n")>previous,"limite do historico")
                        keyboard_progress=uptime()-guest_end
                        assert keyboard_progress>0.1,"PIT nao avancou durante o teste de teclado"
                        assert log.read_bytes().count(READY)==1
                        assert not any(e.get("event") in ("RESET","SHUTDOWN") for e in monitor.events)
                        # Tres resets da mesma VM e desligamento pelo guest.
                        for cycle in range(3):
                            expected=log.read_bytes().count(READY)+1
                            monitor.type_command("reboot")
                            wait(lambda:log.read_bytes().count(READY)==expected,"reboot repetido")
                            monitor.type_command(f"echo reset{cycle}")
                            wait(lambda:f"\r\nreset{cycle}\r\n".encode() in log.read_bytes(),"teclado apos reset")
                        while monitor.receive(0.05): pass
                        reset_events=[e for e in monitor.events if e.get("event")=="RESET" and e.get("data",{}).get("guest")]
                        # Ha firmwares que emitem mais de um RESET entre duas
                        # entradas do kernel; o numero de boots deve ser exato.
                        assert len(reset_events)>=3 and log.read_bytes().count(READY)==4,f"Eventos RESET: {monitor.events!r}"
                        monitor.type_command("shutdown"); process.wait(timeout=15)
                        while monitor.receive(0.05): pass
                        assert process.returncode==0 and any(e.get("event")=="SHUTDOWN" and e.get("data",{}).get("guest") for e in monitor.events)
                        result.update(reboots=3,qmp_reset_events=len(reset_events),shutdown=True,keyboard_burst=True,history_wrap=True,keyboard_timer_seconds=keyboard_progress)
                    if soak:
                        started=time.monotonic(); iteration=0
                        while time.monotonic()-started < soak:
                            iteration+=1
                            for cmd in ("memtest","ps","pmm","vmm","heap",f"echo soak-{iteration}"): monitor.type_command(cmd)
                            wait(lambda:f"\r\nsoak-{iteration}\r\n".encode() in log.read_bytes(),"heartbeat soak")
                            assert log.read_bytes().count(READY)==1 and b"KERNEL PANIC" not in log.read_bytes()
                            assert b"Memtest: FALHOU" not in log.read_bytes()
                            assert not any(e.get("event") in ("RESET","SHUTDOWN") for e in monitor.events)
                            progress={"mode":mode,"elapsed_seconds":time.monotonic()-started,"required_seconds":soak,"iterations":iteration,"complete":False}
                            Path("build/basics-soak-progress.json").write_text(json.dumps(progress,indent=2))
                            time.sleep(min(30,max(0,soak-(time.monotonic()-started))))
                        result.update(continuous_seconds=time.monotonic()-started,iterations=iteration)
                        Path("build/basics-soak-progress.json").write_text(json.dumps({**result,"required_seconds":soak,"complete":True},indent=2))
                    if not controls:
                        assert process.poll() is None and monitor.execute("query-status")["running"]
                        assert not any(e.get("event") in ("RESET","SHUTDOWN") for e in monitor.events)
                    assert b"KERNEL PANIC" not in log.read_bytes()
                result["passed"]=True; results.append(result)
                print(f"OK: basics/{label}/{mode}; {log}",flush=True)
                Path(f"build/basics-{label}-{mode}-result.json").write_text(json.dumps(result,indent=2)+"\n")
                Path("build/basics-soak-results.json" if soak else f"build/basics-results-{mode}.json").write_text(json.dumps(results,indent=2)+"\n")
            finally:
                if monitor:
                    Path(f"build/basics-{label}-{mode}-events.json").write_text(json.dumps(monitor.events,indent=2))
                    monitor.close()
                if process.poll() is None: process.kill()
                process.wait()

if args.soak_seconds:
    run("bios" if args.mode=="all" else args.mode,"soak",soak=args.soak_seconds)
else:
    cases={"boot-bad-revision":1,"boot-no-info":2,"boot-no-hhdm":4,"boot-no-address":4,
           "boot-no-memory":6,"boot-no-framebuffer":None,"boot-no-rsdp":None,"boot-no-modules":None,"boot-no-cmdline":None}
    for mode in (("bios","uefi") if args.mode=="all" else (args.mode,)):
        for resolution in ((640,480),(800,600),(1024,768)): run(mode,f"video-{resolution[0]}x{resolution[1]}",resolution=resolution)
        for option,fatal in cases.items(): run(mode,option,option=option,fatal=fatal)
        run(mode,"controls",controls=True)
if not results: raise SystemExit("Nenhum caso selecionado")
