#include <Service/FlashService.h>
#include <Service/Sched/Checks.h>
#include <Service/Sched/SysTimer.h>
#include <main.h>

using namespace hitcon::service::sched;
using namespace hitcon;

void HAL_FLASH_EndOfOperationCallback(uint32_t ReturnValue) {
  g_flash_service.EndOperationCallback(ReturnValue);
}

void HAL_FLASH_OperationErrorCallback(uint32_t ReturnValue) {
  my_assert(false);
}

namespace hitcon {
FlashService g_flash_service;

FlashService::FlashService()
    : _state(FS_IDLE),
      routine_task(980, (task_callback_t)&FlashService::Routine, this, 20) {}

void FlashService::Init() {
  scheduler.Queue(&routine_task, nullptr);
  scheduler.EnablePeriodic(&routine_task);
}

void* FlashService::GetPagePointer(size_t page_id) {
  if (page_id >= 0 && page_id < FLASH_PAGE_COUNT) {
    size_t res_ptr =
        FLASH_END_ADDR - (FLASH_PAGE_COUNT - page_id) * MY_FLASH_PAGE_SIZE + 1;
    return reinterpret_cast<void*>(res_ptr);
  }
  return nullptr;
}

// static
void FlashService::EndOperationCallback(uint32_t value) {
  if (_state == FS_ERASE_WAIT) {
    _state = FS_ERASE;
  } else if (_state == FS_PROGRAM_WAIT) {
    my_assert(_program_pending_count_ > 0);
    _program_pending_count_--;
  } else {
    my_assert(false);
  }
}

bool FlashService::IsBusy() { return _state != FS_IDLE; }

bool FlashService::ProgramPage(size_t page_id, uint32_t* data, size_t len) {
  if (page_id >= 0 && page_id < FLASH_PAGE_COUNT && len < MY_FLASH_PAGE_SIZE) {
    _addr = reinterpret_cast<size_t>(GetPagePointer(page_id));  // begin address
    _data = data;

    len /= 4;  // save in Word (4Bytes)

    _state = FS_UNLOCK;
    _data_len = len;
    _erase_page_id = 0;
    _program_page_id = 0;

    return true;
  }
  return false;
}

void FlashService::Routine() {
  switch (_state) {
    case FS_IDLE:
      break;
    case FS_UNLOCK:
      HAL_FLASH_Unlock();
      _state = FS_ERASE;
      break;
    case FS_ERASE: {
      if (_erase_page_id == kErasePageCount) {
        _state = FS_PROGRAM;
        break;
      }
      FLASH_EraseInitTypeDef erase_struct = {
          .TypeErase = FLASH_TYPEERASE_PAGES,
          .PageAddress = _addr + _erase_page_id * MY_FLASH_PAGE_SIZE,
          .NbPages = 1,
      };

      _erase_page_id++;

      _state = FS_ERASE_WAIT;

      auto erase_ret = HAL_FLASHEx_Erase_IT(&erase_struct);
      if (erase_ret != HAL_OK) {
        my_assert(false);
      }
      break;
    }
    case FS_ERASE_WAIT:
      if (_wait_cnt == 0) {
        _state = FS_ERASE;
      } else {
        _wait_cnt--;
      }
      break;
    case FS_PROGRAM: {
      _program_pending_count_ = 0;
      _state = FS_PROGRAM_WAIT;
      for (size_t i = 0; i < kProgramPerRun && _program_page_id < _data_len;
           i++, _program_page_id++) {
        size_t addr = _addr + _program_page_id * 4;
        _program_pending_count_++;
        if (HAL_FLASH_Program_IT(FLASH_TYPEPROGRAM_WORD, addr,
                                 _data[_program_page_id]) != HAL_OK) {
          my_assert(false);
        }
      }
      break;
    }
    case FS_PROGRAM_WAIT:
      if (_program_pending_count_ == 0) {
        if (_program_page_id >= _data_len) {
          _state = FS_IDLE;
        } else {
          _state = FS_PROGRAM;
        }
      }
      break;
    default:
      my_assert(false);
  }
}

}  // namespace hitcon
