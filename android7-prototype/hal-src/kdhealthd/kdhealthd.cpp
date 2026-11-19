// kdhealthd: replaces /sbin/healthd, a static binary with the 32-bit binder ABI that aborts on the Kindle
// kernel. Registers "batteryproperties" (AOSP IBatteryPropertiesRegistrar wire format) on the rebuilt
// 64-bit libbinder. Battery state is read only from /sys/class/power_supply; when no battery is
// visible (QEMU) it reports mains power at 100% so Android never decides to shut down for low battery.
#include <batteryservice/BatteryService.h>
#include <batteryservice/IBatteryPropertiesListener.h>
#include <batteryservice/IBatteryPropertiesRegistrar.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <cutils/log.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utils/Mutex.h>
#include <utils/Vector.h>

#undef LOG_TAG
#define LOG_TAG "kdhealthd"

using namespace android;

static const char* kPowerSupply = "/sys/class/power_supply";

static bool readLine(const char* dir, const char* name, char* out, size_t len) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s/%s", kPowerSupply, dir, name);
  FILE* f = fopen(path, "re");
  if (!f) return false;
  bool ok = fgets(out, len, f) != nullptr;
  fclose(f);
  if (ok) out[strcspn(out, "\n")] = 0;
  return ok;
}

static BatteryProperties snapshot() {
  BatteryProperties p;
  p.chargerUsbOnline = p.chargerWirelessOnline = false;
  p.maxChargingCurrent = p.maxChargingVoltage = 0;
  p.batteryCurrent = p.batteryCycleCount = p.batteryFullCharge = p.batteryChargeCounter = 0;
  p.chargerAcOnline = true;
  p.batteryStatus = BATTERY_STATUS_FULL;
  p.batteryHealth = BATTERY_HEALTH_GOOD;
  p.batteryPresent = false;
  p.batteryLevel = 100;
  p.batteryVoltage = 4200;
  p.batteryTemperature = 250;
  p.batteryTechnology = String8("Li-ion");
  DIR* d = opendir(kPowerSupply);
  if (!d) return p;
  bool sawBattery = false, sawCharger = false, chargerOnline = false;
  while (struct dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    char type[32] = "", v[64] = "";
    if (!readLine(e->d_name, "type", type, sizeof type)) continue;
    if (!strcmp(type, "Battery") && !sawBattery) {
      sawBattery = true;
      p.batteryPresent = true;
      if (readLine(e->d_name, "capacity", v, sizeof v)) p.batteryLevel = atoi(v);
      if (readLine(e->d_name, "voltage_now", v, sizeof v)) p.batteryVoltage = atoi(v) / 1000;
      if (readLine(e->d_name, "temp", v, sizeof v)) p.batteryTemperature = atoi(v);
      if (readLine(e->d_name, "status", v, sizeof v)) {
        p.batteryStatus = !strcmp(v, "Charging") ? BATTERY_STATUS_CHARGING
                        : !strcmp(v, "Discharging") ? BATTERY_STATUS_DISCHARGING
                        : !strcmp(v, "Not charging") ? BATTERY_STATUS_NOT_CHARGING
                        : !strcmp(v, "Full") ? BATTERY_STATUS_FULL : BATTERY_STATUS_UNKNOWN;
      }
    } else if (!strcmp(type,"Mains") || !strcmp(type,"Wireless") ||
               !strcmp(type,"USB") || !strcmp(type,"USB_DCP") || !strcmp(type,"USB_CDP") ||
               !strcmp(type,"USB_ACA") || !strcmp(type,"USB_C") || !strcmp(type,"USB_PD") ||
               !strcmp(type,"USB_PD_DRP") || !strcmp(type,"USB_FLOAT")) {
      if (readLine(e->d_name, "online", v, sizeof v)) {
        sawCharger = true;
        if (atoi(v)) {
          chargerOnline = true;
          if (!strncmp(type, "USB", 3)) p.chargerUsbOnline = true;
          else if (!strcmp(type, "Wireless")) p.chargerWirelessOnline = true;
        }
      }
    }
  }
  closedir(d);
  // With a real battery, only a real charger counts as external power.
  if (sawBattery && sawCharger) p.chargerAcOnline = chargerOnline && !p.chargerUsbOnline && !p.chargerWirelessOnline;
  else if (sawBattery) p.chargerAcOnline = p.batteryStatus == BATTERY_STATUS_CHARGING || p.batteryStatus == BATTERY_STATUS_FULL;
  return p;
}

static bool sameProps(const BatteryProperties& a, const BatteryProperties& b) {
  return a.chargerAcOnline == b.chargerAcOnline && a.chargerUsbOnline == b.chargerUsbOnline &&
         a.chargerWirelessOnline == b.chargerWirelessOnline && a.batteryStatus == b.batteryStatus &&
         a.batteryLevel == b.batteryLevel && a.batteryPresent == b.batteryPresent;
}

class Registrar : public BnBatteryPropertiesRegistrar, public IBinder::DeathRecipient {
 public:
  Registrar() : props_(snapshot()) {}

  void registerListener(const sp<IBatteryPropertiesListener>& listener) override {
    {
      Mutex::Autolock l(lock_);
      for (size_t i = 0; i < listeners_.size(); i++)
        if (IInterface::asBinder(listeners_[i]) == IInterface::asBinder(listener)) return;
      listeners_.add(listener);
      IInterface::asBinder(listener)->linkToDeath(this);
    }
    listener->batteryPropertiesChanged(current());
  }

  void unregisterListener(const sp<IBatteryPropertiesListener>& listener) override {
    Mutex::Autolock l(lock_);
    for (size_t i = 0; i < listeners_.size(); i++) {
      if (IInterface::asBinder(listeners_[i]) == IInterface::asBinder(listener)) {
        IInterface::asBinder(listeners_[i])->unlinkToDeath(this);
        listeners_.removeAt(i);
        return;
      }
    }
  }

  status_t getProperty(int id, struct BatteryProperty* val) override {
    BatteryProperties p = current();
    switch (id) {
      case BATTERY_PROP_CAPACITY: val->valueInt64 = p.batteryLevel; return NO_ERROR;
      default: val->valueInt64 = INT64_MIN; return NAME_NOT_FOUND;
    }
  }

  void binderDied(const wp<IBinder>& who) override {
    Mutex::Autolock l(lock_);
    for (size_t i = 0; i < listeners_.size(); i++) {
      if (IInterface::asBinder(listeners_[i]) == who) { listeners_.removeAt(i); return; }
    }
  }

  // Re-read sysfs; notify listeners only when something BatteryService reacts to changed.
  void poll() {
    BatteryProperties fresh = snapshot();
    Vector<sp<IBatteryPropertiesListener> > copy;
    {
      Mutex::Autolock l(lock_);
      if (sameProps(fresh, props_)) return;
      props_ = fresh;
      copy = listeners_;
    }
    for (size_t i = 0; i < copy.size(); i++) copy[i]->batteryPropertiesChanged(fresh);
  }

 private:
  BatteryProperties current() {
    Mutex::Autolock l(lock_);
    return props_;
  }
  Mutex lock_;
  BatteryProperties props_;
  Vector<sp<IBatteryPropertiesListener> > listeners_;
};

int main() {
  sp<ProcessState> ps = ProcessState::self();
  sp<Registrar> registrar = new Registrar();
  status_t st = defaultServiceManager()->addService(String16("batteryproperties"), registrar);
  if (st != NO_ERROR) {
    ALOGE("addService(batteryproperties) failed: %d", st);
    return 1;
  }
  ALOGI("batteryproperties registered");
  ps->startThreadPool();
  for (;;) {
    sleep(5); // Charging/USB changes must reach the status bar promptly, even in a private net namespace.
    registrar->poll();
  }
}
