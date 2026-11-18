#pragma once

namespace terminal {

class Parser {
public:
    template<typename Sink>
    void putc(int c, Sink& sink) {
        if (c == 27) {
            state_ = State::Escape;
            params_[0] = params_[1] = index_ = 0;
            private_ = false;
            return;
        }
        if (state_ == State::Escape) {
            state_ = c == '[' ? State::Csi : State::Text;
            return;
        }
        if (state_ == State::Text) {
            sink.text(c);
            return;
        }
        if (c == '?' && index_ == 0 && params_[0] == 0) {
            private_ = true;
            return;
        }
        if (c >= '0' && c <= '9') {
            if (params_[index_] < 10000)
                params_[index_] = params_[index_] * 10 + static_cast<unsigned int>(c - '0');
            return;
        }
        if (c == ';') {
            if (index_ == 0)
                ++index_;
            return;
        }
        state_ = State::Text;
        if (private_) {
            if (params_[0] == 25 && (c == 'h' || c == 'l'))
                sink.cursor(c == 'h');
        } else if (c == 'H' || c == 'f') {
            sink.position(origin(params_[0]), origin(params_[1]));
        } else if (c == 'G') {
            sink.column(origin(params_[0]));
        } else if (c == 'J' && params_[0] == 2) {
            sink.clear();
        } else if (c == 'K' && (params_[0] == 0 || params_[0] == 2)) {
            sink.erase_line(params_[0]);
        }
    }

    void reset() { state_ = State::Text; }

private:
    enum class State { Text, Escape, Csi };
    State state_{};
    unsigned int params_[2]{};
    unsigned int index_{};
    bool private_{};

    static unsigned int origin(unsigned int one_based) { return one_based ? one_based - 1 : 0; }
};

}  // namespace terminal
