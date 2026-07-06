#include "nas_data_json.h"

#include <string.h>

cJSON *nas_data_to_json(const NasData *data)
{
    if (!data) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON *system = cJSON_CreateObject();
    cJSON_AddNumberToObject(system, "cpu", data->system.cpu_pct);
    cJSON_AddNumberToObject(system, "mem", data->system.ram_pct);
    cJSON_AddNumberToObject(system, "uptime", data->system.uptime_s);
    cJSON_AddNumberToObject(system, "temp_cpu", data->system.temp_cpu);
    cJSON_AddNumberToObject(system, "temp_sys", data->system.temp_sys);
    cJSON_AddNumberToObject(system, "ram_total_mb", data->system.ram_total_mb);
    cJSON_AddNumberToObject(system, "ram_used_mb", data->system.ram_used_mb);
    cJSON_AddStringToObject(system, "hostname", data->system.hostname);
    cJSON_AddStringToObject(system, "model", data->system.model);
    cJSON_AddItemToObject(root, "system", system);

    cJSON *disks = cJSON_CreateArray();
    for (uint8_t i = 0; i < data->disk_count && i < MAX_DISKS; i++) {
        cJSON *disk = cJSON_CreateObject();
        cJSON_AddStringToObject(disk, "name", data->disks[i].name);
        cJSON_AddStringToObject(disk, "device", data->disks[i].device);
        cJSON_AddStringToObject(disk, "model", data->disks[i].model_name);
        cJSON_AddNumberToObject(disk, "temp", data->disks[i].temp);
        cJSON_AddNumberToObject(disk, "size_gb", data->disks[i].size_gb);
        cJSON_AddNumberToObject(disk, "used_gb", data->disks[i].used_gb);
        cJSON_AddNumberToObject(disk, "used_pct", data->disks[i].used_pct);
        cJSON_AddNumberToObject(disk, "health", data->disks[i].health);
        cJSON_AddItemToArray(disks, disk);
    }
    cJSON_AddItemToObject(root, "disks", disks);

    cJSON *volumes = cJSON_CreateArray();
    for (uint8_t i = 0; i < data->volume_count && i < MAX_VOLUMES; i++) {
        cJSON *vol = cJSON_CreateObject();
        cJSON_AddStringToObject(vol, "name", data->volumes[i].name);
        cJSON_AddNumberToObject(vol, "total_gb", data->volumes[i].total_gb);
        cJSON_AddNumberToObject(vol, "used_gb", data->volumes[i].used_gb);
        cJSON_AddNumberToObject(vol, "used_pct", data->volumes[i].used_pct);
        cJSON_AddStringToObject(vol, "raid", data->volumes[i].raid);
        cJSON_AddStringToObject(vol, "status", data->volumes[i].status);
        cJSON_AddItemToArray(volumes, vol);
    }
    cJSON_AddItemToObject(root, "volumes", volumes);

    cJSON *services = cJSON_CreateArray();
    for (uint8_t i = 0; i < data->service_count && i < MAX_SERVICES; i++) {
        cJSON *svc = cJSON_CreateObject();
        cJSON_AddStringToObject(svc, "name", data->services[i].name);
        cJSON_AddBoolToObject(svc, "running", data->services[i].running);
        cJSON_AddBoolToObject(svc, "is_docker", data->services[i].is_docker);
        cJSON_AddItemToArray(services, svc);
    }
    cJSON_AddItemToObject(root, "services", services);

    cJSON *network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "interface", data->network.interface);
    cJSON_AddStringToObject(network, "ip", data->network.ip);
    cJSON_AddNumberToObject(network, "rx_bps", data->network.rx_bps);
    cJSON_AddNumberToObject(network, "tx_bps", data->network.tx_bps);
    cJSON_AddItemToObject(root, "network", network);

    cJSON_AddBoolToObject(root, "is_online", data->is_online);
    cJSON_AddNumberToObject(root, "last_update_ms", data->last_update_ms);

    return root;
}

bool nas_json_to_data(cJSON *json, NasData *data)
{
    if (!json || !data) return false;

    memset(data, 0, sizeof(NasData));

    cJSON *system = cJSON_GetObjectItem(json, "system");
    if (cJSON_IsObject(system)) {
        cJSON *hostname = cJSON_GetObjectItem(system, "hostname");
        if (cJSON_IsString(hostname)) {
            strncpy(data->system.hostname, hostname->valuestring, sizeof(data->system.hostname) - 1);
        }
        cJSON *model = cJSON_GetObjectItem(system, "model");
        if (cJSON_IsString(model)) {
            strncpy(data->system.model, model->valuestring, sizeof(data->system.model) - 1);
        }
        cJSON *cpu = cJSON_GetObjectItem(system, "cpu");
        if (cJSON_IsNumber(cpu)) data->system.cpu_pct = (float)cpu->valuedouble;
        cJSON *mem = cJSON_GetObjectItem(system, "mem");
        if (cJSON_IsNumber(mem)) data->system.ram_pct = (float)mem->valuedouble;
        cJSON *uptime = cJSON_GetObjectItem(system, "uptime");
        if (cJSON_IsNumber(uptime)) data->system.uptime_s = (uint32_t)uptime->valueint;
        cJSON *temp_cpu = cJSON_GetObjectItem(system, "temp_cpu");
        if (cJSON_IsNumber(temp_cpu)) data->system.temp_cpu = (int16_t)temp_cpu->valueint;
        cJSON *temp_sys = cJSON_GetObjectItem(system, "temp_sys");
        if (cJSON_IsNumber(temp_sys)) data->system.temp_sys = (int16_t)temp_sys->valueint;
        cJSON *ram_total = cJSON_GetObjectItem(system, "ram_total_mb");
        if (cJSON_IsNumber(ram_total)) data->system.ram_total_mb = (uint32_t)ram_total->valueint;
        cJSON *ram_used = cJSON_GetObjectItem(system, "ram_used_mb");
        if (cJSON_IsNumber(ram_used)) data->system.ram_used_mb = (uint32_t)ram_used->valueint;
    }

    cJSON *disks = cJSON_GetObjectItem(json, "disks");
    if (cJSON_IsArray(disks)) {
        int count = cJSON_GetArraySize(disks);
        for (int i = 0; i < count && i < MAX_DISKS; i++) {
            cJSON *d = cJSON_GetArrayItem(disks, i);
            if (!cJSON_IsObject(d)) continue;
            cJSON *name = cJSON_GetObjectItem(d, "name");
            if (cJSON_IsString(name)) strncpy(data->disks[i].name, name->valuestring, sizeof(data->disks[i].name) - 1);
            cJSON *device = cJSON_GetObjectItem(d, "device");
            if (cJSON_IsString(device)) strncpy(data->disks[i].device, device->valuestring, sizeof(data->disks[i].device) - 1);
            cJSON *model = cJSON_GetObjectItem(d, "model");
            if (cJSON_IsString(model)) strncpy(data->disks[i].model_name, model->valuestring, sizeof(data->disks[i].model_name) - 1);
            cJSON *temp = cJSON_GetObjectItem(d, "temp");
            if (cJSON_IsNumber(temp)) data->disks[i].temp = (int16_t)temp->valueint;
            cJSON *size = cJSON_GetObjectItem(d, "size_gb");
            if (cJSON_IsNumber(size)) data->disks[i].size_gb = (uint32_t)size->valueint;
            cJSON *used = cJSON_GetObjectItem(d, "used_gb");
            if (cJSON_IsNumber(used)) data->disks[i].used_gb = (uint32_t)used->valueint;
            cJSON *used_pct = cJSON_GetObjectItem(d, "used_pct");
            if (cJSON_IsNumber(used_pct)) data->disks[i].used_pct = (uint8_t)used_pct->valueint;
            cJSON *health = cJSON_GetObjectItem(d, "health");
            if (cJSON_IsNumber(health)) data->disks[i].health = (HealthStatus)health->valueint;
            data->disk_count++;
        }
    }

    cJSON *volumes = cJSON_GetObjectItem(json, "volumes");
    if (cJSON_IsArray(volumes)) {
        int count = cJSON_GetArraySize(volumes);
        for (int i = 0; i < count && i < MAX_VOLUMES; i++) {
            cJSON *v = cJSON_GetArrayItem(volumes, i);
            if (!cJSON_IsObject(v)) continue;
            cJSON *name = cJSON_GetObjectItem(v, "name");
            if (cJSON_IsString(name)) strncpy(data->volumes[i].name, name->valuestring, sizeof(data->volumes[i].name) - 1);
            cJSON *total = cJSON_GetObjectItem(v, "total_gb");
            if (cJSON_IsNumber(total)) data->volumes[i].total_gb = (uint32_t)total->valueint;
            cJSON *used = cJSON_GetObjectItem(v, "used_gb");
            if (cJSON_IsNumber(used)) data->volumes[i].used_gb = (uint32_t)used->valueint;
            cJSON *used_pct = cJSON_GetObjectItem(v, "used_pct");
            if (cJSON_IsNumber(used_pct)) data->volumes[i].used_pct = (uint8_t)used_pct->valueint;
            cJSON *raid = cJSON_GetObjectItem(v, "raid");
            if (cJSON_IsString(raid)) strncpy(data->volumes[i].raid, raid->valuestring, sizeof(data->volumes[i].raid) - 1);
            cJSON *status = cJSON_GetObjectItem(v, "status");
            if (cJSON_IsString(status)) strncpy(data->volumes[i].status, status->valuestring, sizeof(data->volumes[i].status) - 1);
            data->volume_count++;
        }
    }

    cJSON *services = cJSON_GetObjectItem(json, "services");
    if (cJSON_IsArray(services)) {
        int count = cJSON_GetArraySize(services);
        for (int i = 0; i < count && i < MAX_SERVICES; i++) {
            cJSON *s = cJSON_GetArrayItem(services, i);
            if (!cJSON_IsObject(s)) continue;
            cJSON *name = cJSON_GetObjectItem(s, "name");
            if (cJSON_IsString(name)) strncpy(data->services[i].name, name->valuestring, sizeof(data->services[i].name) - 1);
            cJSON *running = cJSON_GetObjectItem(s, "running");
            if (cJSON_IsBool(running)) data->services[i].running = cJSON_IsTrue(running);
            cJSON *is_docker = cJSON_GetObjectItem(s, "is_docker");
            if (cJSON_IsBool(is_docker)) data->services[i].is_docker = cJSON_IsTrue(is_docker);
            data->service_count++;
        }
    }

    cJSON *network = cJSON_GetObjectItem(json, "network");
    if (cJSON_IsObject(network)) {
        cJSON *iface = cJSON_GetObjectItem(network, "interface");
        if (cJSON_IsString(iface)) strncpy(data->network.interface, iface->valuestring, sizeof(data->network.interface) - 1);
        cJSON *ip = cJSON_GetObjectItem(network, "ip");
        if (cJSON_IsString(ip)) strncpy(data->network.ip, ip->valuestring, sizeof(data->network.ip) - 1);
        cJSON *rx = cJSON_GetObjectItem(network, "rx_bps");
        if (cJSON_IsNumber(rx)) data->network.rx_bps = (uint32_t)rx->valueint;
        cJSON *tx = cJSON_GetObjectItem(network, "tx_bps");
        if (cJSON_IsNumber(tx)) data->network.tx_bps = (uint32_t)tx->valueint;
    }

    cJSON *online = cJSON_GetObjectItem(json, "is_online");
    if (cJSON_IsBool(online)) data->is_online = cJSON_IsTrue(online);
    cJSON *last_update = cJSON_GetObjectItem(json, "last_update_ms");
    if (cJSON_IsNumber(last_update)) data->last_update_ms = (uint32_t)last_update->valueint;

    return true;
}

void nas_data_free_json(cJSON *json)
{
    if (json) {
        cJSON_Delete(json);
    }
}
