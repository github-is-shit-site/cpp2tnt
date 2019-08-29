#include "tntevloop.h"

#include <functional>
#include <iostream>

using namespace std;

namespace tnt
{

    TntEvLoop::TntEvLoop()
    {
        on_opened(bind(&TntEvLoop::OnConnected, this));
    }

    void TntEvLoop::Attach(struct ev_loop* loop)
    {
        loop_ = loop;

        socketWatcher_.data = this;
        ev_init(&socketWatcher_, OnSocketEvent_);
        on_socket_watcher_request(bind(&TntEvLoop::OnSocketWatcherRequest, this, placeholders::_1));

        ev_async_init(&asyncNotifier_, OnAsyncNotifier_);
        asyncNotifier_.data = this;
        ev_async_start(loop, &asyncNotifier_);

        ev_timer_init(&timer_, OnTimer_, 1, 1);
        timer_.data = this;
        ev_timer_start(loop, &timer_);

        on_notify_request(bind(ev_async_send, loop, &asyncNotifier_));

    }

    void TntEvLoop::OnSocketWatcherRequest(int mode)
    {
        int events = (mode & tnt::socket_state::read  ? EV_READ  : EV_NONE);
        events    |= (mode & tnt::socket_state::write ? EV_WRITE : EV_NONE);

        if ((socketWatcher_.events & (EV_READ | EV_WRITE)) != events)
        {
            if (ev_is_active(&socketWatcher_))
                ev_io_stop(loop_, &socketWatcher_);
            ev_io_set(&socketWatcher_, socket_handle(), events);
            if (events)
                ev_io_start(loop_, &socketWatcher_);

            if (mode & tnt::socket_state::read)
                cout << "R";
            if (mode & tnt::socket_state::write)
                cout << "W";
            if (!mode)
                cout << "N";
            cout << endl;
        }

    }

    void TntEvLoop::OnSocketEvent_(struct ev_loop* loop, ev_io* w, int revents)
    {
        ((TntEvLoop*)w->data)->OnSocketEvent(loop, w, revents);
    }

    void TntEvLoop::OnSocketEvent(struct ev_loop* loop, ev_io* w, int revents)
    {
        (void)loop;(void)w;
        if (revents & EV_ERROR)
            handle_error("EV_ERROR soket state received");

        if (revents & EV_WRITE)
            write();
        if (revents & EV_READ)
            read();
    }

    void TntEvLoop::OnAsyncNotifier_(struct ev_loop* loop, ev_async* w, int revents)
    {
        ((TntEvLoop*)w->data)->OnAsyncNotifier(loop, w, revents);
    }

    void TntEvLoop::OnAsyncNotifier(struct ev_loop* loop, ev_async* w, int revents)
    {
        (void)loop;(void)w;(void)revents;
        acquire_notifications();
    }

    void TntEvLoop::OnConnected()
    {
    }

    void TntEvLoop::OnTimer_(struct ev_loop* loop, ev_timer* w, int revents)
    {
        ((TntEvLoop*)w->data)->OnTimer(loop, w, revents);
    }

    void TntEvLoop::OnTimer(struct ev_loop*, ev_timer*, int)
    {
        tick_1sec();
    }



}
