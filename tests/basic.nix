{ pkgs, self }:
pkgs.testers.nixosTest {
  nodes.machine =
    { pkgs, ... }:
    {
      imports = [
        self.nixosModules.deadbeef-pxtone
        (self.inputs.nixpkgs + "/nixos/tests/common/x11.nix")
        (self.inputs.nixpkgs + "/nixos/tests/common/user-account.nix")
      ];
      test-support.displayManager.auto.user = "alice";
      services.xserver.enable = true;
      environment.systemPackages = [
        (pkgs.deadbeef-with-plugins.override {
          plugins = [ pkgs.deadbeefPlugins.pxtone ];
        })
      ];
      hardware.alsa = {
        enable = true;
        enableRecorder = true;
        defaultDevice.playback = "pcm.recorder";
      };
      systemd.services.audio-recorder = {
        script = "${pkgs.alsa-utils}/bin/arecord -Drecorder -fS16_LE -r48000 -c2 /tmp/record.wav";
      };
      system.stateVersion = "24.11";
    };
  name = "deadbeef-pxtone-basic";
  testScript =
    { nodes, ... }:
    let
      user = nodes.machine.users.users.alice;
    in
    ''
      from contextlib import contextmanager

      @contextmanager
      def record_audio(m):
        m.systemctl("start audio-recorder")
        yield
        m.systemctl("stop audio-recorder")

      def wait_for_sound(m):
        m.wait_for_file("/tmp/record.wav")
        while True:
          m.execute("tail -c 2M /tmp/record.wav > /tmp/last")
          size = int(m.succeed("stat -c '%s' /tmp/last").strip())
          status, output = m.execute(
            f"cmp -i 50 -n {size - 50} /tmp/last /dev/zero 2>&1"
          )
          if status == 1:
            break
          m.sleep(2)

      machine.wait_for_x()
      machine.wait_for_file("${user.home}/.Xauthority")
      machine.succeed("xauth merge ${user.home}/.Xauthority")

      with subtest("Wait until DeaDBeeF starts up"):
        with record_audio(machine):
          machine.copy_from_host("${./sample.ptcop}", "/tmp/test.ptcop")
          machine.execute("su - alice -c 'xterm -e deadbeef /tmp/test.ptcop' >&2 &")
          machine.wait_for_window("DeaDBeeF")
          machine.sleep(1)
          wait_for_sound(machine)
    '';
}
