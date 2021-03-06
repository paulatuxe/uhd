//
// Copyright 2010-2011 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "usrp1_impl.hpp"
#include "fpga_regs_standard.h"
#include <uhd/utils/msg.hpp>
#include <uhd/usrp/dsp_utils.hpp>
#include <uhd/usrp/dsp_props.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>
#include <cmath>

using namespace uhd;
using namespace uhd::usrp;

/***********************************************************************
 * RX DDC Initialization
 **********************************************************************/
void usrp1_impl::rx_dsp_init(void)
{
    for (size_t i = 0; i < this->get_num_ddcs(); i++){
        _rx_dsp_proxies[str(boost::format("DSP%d")%i)] = wax_obj_proxy::make(
            boost::bind(&usrp1_impl::rx_dsp_get, this, _1, _2, i),
            boost::bind(&usrp1_impl::rx_dsp_set, this, _1, _2, i)
        );
        rx_dsp_set(DSP_PROP_HOST_RATE, _clock_ctrl->get_master_clock_freq() / 16, i);
    }
}

/***********************************************************************
 * RX DDC Get
 **********************************************************************/
void usrp1_impl::rx_dsp_get(const wax::obj &key_, wax::obj &val, size_t which_dsp){
    named_prop_t key = named_prop_t::extract(key_);

    switch(key.as<dsp_prop_t>()){
    case DSP_PROP_NAME:
        val = str(boost::format("usrp1 ddc%d %s")
            % which_dsp
            % (this->has_rx_halfband()? "+ hb" : "")
        );
        return;

    case DSP_PROP_OTHERS:
        val = prop_names_t();
        return;

    case DSP_PROP_FREQ_SHIFT:
        val = _rx_dsp_freqs[which_dsp];
        return;

    case DSP_PROP_CODEC_RATE:
        val = _clock_ctrl->get_master_clock_freq();
        return;

    case DSP_PROP_HOST_RATE:
        val = _clock_ctrl->get_master_clock_freq()/_rx_dsp_decim;
        return;

    default: UHD_THROW_PROP_GET_ERROR();
    }

}

/***********************************************************************
 * RX DDC Set
 **********************************************************************/
void usrp1_impl::rx_dsp_set(const wax::obj &key_, const wax::obj &val, size_t which_dsp){
    named_prop_t key = named_prop_t::extract(key_);

    switch(key.as<dsp_prop_t>()) {
    case DSP_PROP_FREQ_SHIFT: {
            double new_freq = val.as<double>();
            boost::uint32_t reg_word = dsp_type1::calc_cordic_word_and_update(
                new_freq, _clock_ctrl->get_master_clock_freq());

            static const boost::uint32_t dsp_index_to_reg_val[4] = {
                FR_RX_FREQ_0, FR_RX_FREQ_1, FR_RX_FREQ_2, FR_RX_FREQ_3
            };
            _iface->poke32(dsp_index_to_reg_val[which_dsp], ~reg_word + 1);
            _rx_dsp_freqs[which_dsp] = new_freq;
            return;
        }

    case DSP_PROP_HOST_RATE:
        if (which_dsp != 0) return; //only for dsp[0] as this is vectorized
        {
            size_t rate = size_t(_clock_ctrl->get_master_clock_freq() / val.as<double>());

            //clip the rate to something in range:
            rate = std::min<size_t>(std::max<size_t>(rate, 4), 256);

            _rx_dsp_decim = rate;
            //TODO Poll every 100ms. Make it selectable?
            _rx_samps_per_poll_interval = size_t(0.1 * _clock_ctrl->get_master_clock_freq() / rate);

            bool s = this->disable_rx();
            _iface->poke32(FR_DECIM_RATE, _rx_dsp_decim/2 - 1);
            this->restore_rx(s);
        }
        return;

    case DSP_PROP_STREAM_CMD:
        if (which_dsp != 0) return; //only for dsp[0] as this is vectorized
        _soft_time_ctrl->issue_stream_cmd(val.as<stream_cmd_t>());
        return;

    default: UHD_THROW_PROP_SET_ERROR();
    }

}

/***********************************************************************
 * TX DUC Initialization
 **********************************************************************/
void usrp1_impl::tx_dsp_init(void)
{
    for (size_t i = 0; i < this->get_num_ducs(); i++){
        _tx_dsp_proxies[str(boost::format("DSP%d")%i)] = wax_obj_proxy::make(
            boost::bind(&usrp1_impl::tx_dsp_get, this, _1, _2, i),
            boost::bind(&usrp1_impl::tx_dsp_set, this, _1, _2, i)
        );
        tx_dsp_set(DSP_PROP_HOST_RATE, _clock_ctrl->get_master_clock_freq() / 16, i);
    }
}

/***********************************************************************
 * TX DUC Get
 **********************************************************************/
void usrp1_impl::tx_dsp_get(const wax::obj &key_, wax::obj &val, size_t which_dsp){
    named_prop_t key = named_prop_t::extract(key_);

    switch(key.as<dsp_prop_t>()) {
    case DSP_PROP_NAME:
        val = str(boost::format("usrp1 duc%d %s")
            % which_dsp
            % (this->has_tx_halfband()? "+ hb" : "")
        );
        return;

    case DSP_PROP_OTHERS:
        val = prop_names_t(); //empty
        return;

    case DSP_PROP_FREQ_SHIFT:
        val = _tx_dsp_freqs[which_dsp];
        return;

    case DSP_PROP_CODEC_RATE:
        val = _clock_ctrl->get_master_clock_freq();
        return;

    case DSP_PROP_HOST_RATE:
        val = _clock_ctrl->get_master_clock_freq() / _tx_dsp_interp;
        return;

    default: UHD_THROW_PROP_GET_ERROR();
    }

}

/***********************************************************************
 * TX DUC Set
 **********************************************************************/
void usrp1_impl::tx_dsp_set(const wax::obj &key_, const wax::obj &val, size_t which_dsp){
    named_prop_t key = named_prop_t::extract(key_);

    switch(key.as<dsp_prop_t>()) {

    case DSP_PROP_FREQ_SHIFT: {
            double new_freq = val.as<double>();

            //map the freq shift key to a subdev spec to a particular codec chip
            std::string db_name = _tx_subdev_spec.at(which_dsp).db_name;
            if (db_name == "A") _codec_ctrls[DBOARD_SLOT_A]->set_duc_freq(new_freq);
            if (db_name == "B") _codec_ctrls[DBOARD_SLOT_B]->set_duc_freq(new_freq);

            _tx_dsp_freqs[which_dsp] = new_freq;
            return;
        }

    case DSP_PROP_HOST_RATE:
        if (which_dsp != 0) return; //only for dsp[0] as this is vectorized
        {
            size_t rate = size_t(_clock_ctrl->get_master_clock_freq() / val.as<double>());

            //clip the rate to something in range:
            rate = std::min<size_t>(std::max<size_t>(rate, 4), 256);

            _tx_dsp_interp = rate;

            //TODO Poll every 100ms. Make it selectable? 
            _tx_samps_per_poll_interval = size_t(0.1 * _clock_ctrl->get_master_clock_freq() / rate);

            bool s = this->disable_tx();
            _iface->poke32(FR_INTERP_RATE, _tx_dsp_interp/2 - 1);
            this->restore_tx(s);
            return;
        }
    default: UHD_THROW_PROP_SET_ERROR();
    }

}
