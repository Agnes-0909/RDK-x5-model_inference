#define private public
#include "manager_app.hpp"
#undef private

#include <vector>

#include "server_types.hpp"

int main() {
  mt::BatchResultMessage left{};
  left.batch_id = 42;
  left.channel_results[0].channel_id = 0;
  left.channel_results[0].frame_id = 100;
  left.channel_results[0].status_code = mt::kStatusOk;
  left.channel_results[2].channel_id = 2;
  left.channel_results[2].frame_id = 102;
  left.channel_results[2].status_code = mt::kStatusOk;

  mt::BatchResultMessage right{};
  right.batch_id = 42;
  right.channel_results[1].channel_id = 1;
  right.channel_results[1].frame_id = 101;
  right.channel_results[1].status_code = mt::kStatusOk;

  mt::BatchResultMessage merged{};
  if (!mt::ManagerApp::mergeBatchResult({left, right}, 42, merged)) {
    return 1;
  }

  if (merged.batch_id != 42 || merged.overall_status != mt::kStatusOk) {
    return 2;
  }
  if (merged.channel_results[0].frame_id != 100 ||
      merged.channel_results[1].frame_id != 101 ||
      merged.channel_results[2].frame_id != 102) {
    return 3;
  }

  return 0;
}
