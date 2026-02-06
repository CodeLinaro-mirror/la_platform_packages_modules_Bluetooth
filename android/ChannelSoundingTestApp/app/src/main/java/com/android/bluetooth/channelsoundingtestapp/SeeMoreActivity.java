 /*
*Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
*SPDX-License-Identifier: BSD-3-Clause-Clear
 */
package com.android.bluetooth.channelsoundingtestapp;

import android.os.Bundle;
import android.widget.TextView;
import android.widget.Button;
import androidx.appcompat.app.AppCompatActivity;
import android.os.Bundle;
import android.widget.TextView;
import android.widget.Button;

public class SeeMoreActivity extends AppCompatActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.layout_see_more);

        TextView tv = findViewById(R.id.distance_value);
        InitiatorViewModel.getLiveDistanceSingleton().observe(this, value -> {
            Double distance = (value != null) ? (Double) value : null;
            if (distance != null && distance >= 0) {
                tv.setText(String.format("%.1f", distance));
                int color;
                if (distance > 50) {
                    color = 0xFF00FF00; // Green
                } else if (distance > 10) {
                    color = 0xFFFFFF00; // Yellow
                } else if (distance > 2) {
                    color = 0xFFFF9800; // Orange
                } else {
                    color = 0xFFFF0000; // Red
                }
                tv.setTextColor(color);
            } else {
                tv.setText("-");
                tv.setTextColor(0xFFDDDDDD);
            }
        });

        Button btnSeeLess = findViewById(R.id.btn_see_less);
        btnSeeLess.setOnClickListener(v -> finish());
    }
}
