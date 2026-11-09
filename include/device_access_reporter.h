#pragma once

// Schedule platform registration after Wi-Fi connects; HTTP runs in a worker task.
void device_access_report_loop();
